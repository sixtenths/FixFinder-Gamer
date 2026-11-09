#include "episode_maps.h"
#include <string.h>
#include "gd32f4xx_fmc.h"
#include "lprintf.h"
#include "storage/ff.h"
#include "w_wad.h"

enum { MAP_COUNT = 9, MAP_SLOT_BYTES = 256 * 1024 };
extern const uint8_t __fixfinder_map_slot[];
extern uint8_t __doom_line_scratch[], __doom_line_scratch_end[];

struct map_record {
    uint32_t offset, length, crc;
};

struct map_table {
    char magic[8];
    uint32_t count;
    struct map_record maps[MAP_COUNT];
};

_Static_assert(sizeof(struct map_table) == 120, "map pack header layout");
static uint32_t staged_length;

static uint32_t update_crc(uint32_t crc, const uint8_t *bytes, uint32_t length)
{
    while (length--) {
        crc ^= *bytes++;
        for (unsigned int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
    }
    return crc;
}

static bool read_exact(FIL *file, void *data, UINT length)
{
    UINT got;
    return f_read(file, data, length, &got) == FR_OK && got == length;
}

static bool open_pack(FIL *file, struct map_table *table)
{
    const int lump = W_CheckNumForName("FFMAPTAB");
    if (lump < 0 || W_LumpLength(lump) != sizeof *table ||
        f_open(file, "0:/FFDOOM1.PAK", FA_READ) != FR_OK)
        return false;

    bool ok = read_exact(file, table, sizeof *table) &&
              memcmp(table, W_CacheLumpNum(lump), sizeof *table) == 0 &&
              memcmp(table->magic, "FFDOOM1", 8) == 0 && table->count == MAP_COUNT;
    uint32_t end = sizeof *table;
    for (unsigned int i = 0; ok && i < MAP_COUNT; ++i) {
        const struct map_record *record = &table->maps[i];
        ok = record->offset == end && record->length && !(record->length & 3U) &&
             record->length <= MAP_SLOT_BYTES && end <= f_size(file) &&
             record->length <= f_size(file) - end;
        if (ok) end += record->length;
    }
    if (ok && end == f_size(file)) return true;
    f_close(file);
    return false;
}

bool doom_maps_available(void)
{
    FIL file;
    struct map_table table;
    if (!open_pack(&file, &table)) return false;
    return f_close(&file) == FR_OK;
}

static bool stage_record(FIL *file, const struct map_record *record)
{
    const uintptr_t base = (uintptr_t)__fixfinder_map_slot;
    if (base != 0x082c0000U) return false;
    if (~update_crc(UINT32_MAX, __fixfinder_map_slot, record->length) == record->crc)
        return true;

    uint32_t block[256];
    uint32_t crc = UINT32_MAX;
    if (f_lseek(file, record->offset) != FR_OK) return false;
    for (uint32_t off = 0; off < record->length;) {
        const uint32_t size = record->length - off < sizeof block ? record->length - off : sizeof block;
        if (!read_exact(file, block, size)) return false;
        crc = update_crc(crc, (const uint8_t *)block, size);
        off += size;
    }
    if (~crc != record->crc || f_lseek(file, record->offset) != FR_OK) return false;

    fmc_unlock();
    fmc_flag_clear(FMC_FLAG_END | FMC_FLAG_OPERR | FMC_FLAG_WPERR |
                   FMC_FLAG_PGMERR | FMC_FLAG_PGSERR | FMC_FLAG_RDDERR);
    FMC_CTL = (FMC_CTL & ~FMC_CTL_PSZ) | CTL_PSZ_WORD;
    bool ok = fmc_sector_erase(CTL_SN(15)) == FMC_READY;
    for (uint32_t off = 0; ok && off < record->length;) {
        const uint32_t size = record->length - off < sizeof block ? record->length - off : sizeof block;
        ok = read_exact(file, block, size);
        for (uint32_t i = 0; ok && i < size / sizeof *block; ++i) {
            if (block[i] != UINT32_MAX)
                ok = fmc_word_program(base + off + i * sizeof *block, block[i]) == FMC_READY;
        }
        off += size;
    }
    fmc_lock();
    return ok && ~update_crc(UINT32_MAX, __fixfinder_map_slot, record->length) == record->crc;
}

void doom_stage_map(int episode, int map)
{
    FIL file;
    struct map_table table;
    if (episode != 1 || map < 1 || map > MAP_COUNT || !open_pack(&file, &table))
        I_Error("Episode 1 map pack is missing or invalid");

    staged_length = 0;
    const struct map_record *record = &table.maps[map - 1];
    const bool ok = stage_record(&file, record);
    f_close(&file);
    if (!ok) I_Error("Could not verify E1M%d map data", map);
    staged_length = record->length;
}

const void *doom_staged_lump(uint32_t offset, uint32_t size)
{
    if (offset > staged_length || size > staged_length - offset)
        I_Error("Invalid staged map lump");
    return __fixfinder_map_slot + offset;
}

void *doom_map_line_storage(uint32_t length)
{
    if (length > (uintptr_t)__doom_line_scratch_end - (uintptr_t)__doom_line_scratch)
        return NULL;
    return memset(__doom_line_scratch, 0, length);
}
