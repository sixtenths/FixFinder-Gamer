#include "package.h"
#include "../usb-loader/screen.h"
#include "drv_usb_hw.h"
#include "gd32f4xx_fmc.h"
#include <string.h>

struct sector { uint32_t start; uint8_t selector; };
static const struct sector sectors[] = {
    {0x08010000, 4}, {0x08020000, 5}, {0x08040000, 6}, {0x08060000, 7},
    {0x08080000, 8}, {0x080a0000, 9}, {0x080c0000, 10}, {0x080e0000, 11},
    {0x08100000, 16}, {0x08104000, 17}, {0x08108000, 18}, {0x0810c000, 19},
    {0x08110000, 20}, {0x08120000, 21}, {0x08140000, 22}, {0x08160000, 23},
    {0x08180000, 24}, {0x081a0000, 25}, {0x081c0000, 26}, {0x081e0000, 27},
    {0x08200000, 12}, {0x08240000, 13}, {0x08280000, 14}, {SWITCH_LIMIT, 0}
};
#define SECTOR_COUNT (sizeof sectors / sizeof *sectors - 1U)
_Static_assert(SECTOR_COUNT <= 32, "dirty sector mask");

static uint32_t block[512];
static FATFS storage;
static bool panel_ready;

extern void usb_timer_irq(void);

void TIMER2_IRQHandler(void)
{
    usb_timer_irq();
}

static uint32_t crc_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    static const uint32_t table[] = {
        0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
        0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
        0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c
    };
    while (size--) {
        crc ^= *data++;
        crc = (crc >> 4) ^ table[crc & 15U];
        crc = (crc >> 4) ^ table[crc & 15U];
    }
    return crc;
}

static bool slot_valid(uint32_t game)
{
    const struct switch_image *image = (const void *)SWITCH_SLOT;
    const uint32_t *vectors = (const void *)SWITCH_APP;
    return switch_image_ok(image, game) &&
           switch_vectors_ok(vectors[0], vectors[1], image->size) &&
           ~crc_update(UINT32_MAX, (const void *)SWITCH_APP, image->size) == image->crc;
}

static uint32_t sector_end(unsigned int sector, uint32_t image_end)
{
    uint32_t end = sectors[sector + 1].start;
    return end < image_end ? end : image_end;
}

/* Read the entire source before erasing, and skip sectors already matching it.
 * Arcade sectors beyond Sonic's end are left intact on a switch to Sonic. */
static bool scan(FIL *file, const struct switch_image *image, uint32_t *dirty)
{
    uint32_t crc = UINT32_MAX, address = SWITCH_APP;
    uint32_t end = SWITCH_APP + image->size;
    *dirty = 1U;
    for (unsigned int sector = 0; sector < SECTOR_COUNT && address < end; ++sector) {
        uint32_t limit = sector_end(sector, end);
        while (address < limit) {
            unsigned int size = limit - address < sizeof block ? limit - address : sizeof block;
            if (!switch_read(file, block, size)) return false;
            crc = crc_update(crc, (const uint8_t *)block, size);
            if (!(*dirty & (1U << sector)) && memcmp((const void *)address, block, size))
                *dirty |= 1U << sector;
            address += size;
        }
    }
    return address == end && ~crc == image->crc;
}

static bool program(uint32_t address, uint32_t word)
{
    return (word == UINT32_MAX || fmc_word_program(address, word) == FMC_READY) &&
           *(const volatile uint32_t *)address == word;
}

static void draw(enum loader_view view, unsigned int progress)
{
    if (panel_ready) loader_screen_draw(view, progress);
}

static bool stage(FIL *file, const struct switch_image *image, uint32_t dirty)
{
    uint32_t end = SWITCH_APP + image->size;
    draw(VIEW_INSTALL, 0);
    fmc_unlock();
    fmc_flag_clear(FMC_FLAG_END | FMC_FLAG_OPERR | FMC_FLAG_WPERR |
                   FMC_FLAG_PGMERR | FMC_FLAG_PGSERR | FMC_FLAG_RDDERR);
    FMC_CTL = (FMC_CTL & ~FMC_CTL_PSZ) | CTL_PSZ_WORD;
    bool ok = true;
    for (unsigned int i = 0; ok && i < SECTOR_COUNT && sectors[i].start < end; ++i) {
        if (!(dirty & (1U << i))) continue;
        uint32_t address = i ? sectors[i].start : SWITCH_APP;
        const uint32_t limit = sector_end(i, end);
        ok = f_lseek(file, sizeof *image + address - SWITCH_APP) == FR_OK &&
             fmc_sector_erase(CTL_SN(sectors[i].selector)) == FMC_READY;
        while (ok && address < limit) {
            unsigned int size = limit - address < sizeof block ? limit - address : sizeof block;
            ok = switch_read(file, block, size);
            for (unsigned int word = 0; ok && word < size / 4; ++word)
                ok = program(address + 4 * word, block[word]);
            address += size;
        }
        if (ok) draw(VIEW_INSTALL, (limit - SWITCH_APP) * 100U / image->size);
    }
    if (ok) {
        draw(VIEW_VERIFY, 100);
        ok = ~crc_update(UINT32_MAX, (const void *)SWITCH_APP, image->size) == image->crc &&
             program(SWITCH_SLOT + 4, image->game) &&
             program(SWITCH_SLOT + 8, image->size) &&
             program(SWITCH_SLOT + 12, image->crc) &&
             program(SWITCH_SLOT, SWITCH_MAGIC);
    }
    fmc_lock();
    return ok;
}

static bool install(uint32_t game)
{
    struct switch_paths paths = switch_paths(game);
    const char *choices[] = {paths.current, paths.backup, paths.temporary};
    for (unsigned int i = 0; i < sizeof choices / sizeof *choices; ++i) {
        FIL file;
        struct switch_image image;
        if (!switch_open(&file, choices[i], game, &image)) continue;
        uint32_t dirty;
        bool ok = scan(&file, &image, &dirty) && stage(&file, &image, dirty);
        f_close(&file);
        if (ok) return true;
    }
    return false;
}

__attribute__((naked, noreturn)) static void jump(uint32_t stack, uint32_t entry)
{
    __asm volatile("msr msp, r0\n"
                   "bx r1");
}

__attribute__((noreturn)) static void boot_slot(void)
{
    const uint32_t *vectors = (const void *)SWITCH_APP;
    SCB->VTOR = SWITCH_APP;
    __DSB();
    __ISB();
    jump(vectors[0], vectors[1]);
}

int main(void)
{
    uint32_t request = SWITCH_COOKIE[0];
    if (SWITCH_COOKIE[1] != ~request) request = 0;
    SWITCH_COOKIE[0] = SWITCH_COOKIE[1] = 0;
    uint32_t game = request & ~SWITCH_RUN;
    if (!switch_game_known(game)) game = SWITCH_ARCADE;
    if (slot_valid(game)) boot_slot();

    extern const uint32_t __gVectors[];
    SCB->VTOR = (uintptr_t)__gVectors;
    usb_timer_init();
    __enable_irq();
    panel_ready = loader_screen_init();
    fixfinder_emmc_probe();
    bool mounted = fixfinder_library_write_allowed() &&
                   f_mount(&storage, "0:", 1) == FR_OK && storage.fs_type == FS_FAT32;
    if (mounted && install(game))
        switch_reset(SWITCH_RUN | game, BOOT_GAME);
    if (slot_valid(SWITCH_ARCADE) || (mounted && game != SWITCH_ARCADE && install(SWITCH_ARCADE)))
        switch_reset(SWITCH_RUN | SWITCH_ARCADE, BOOT_GAME);

    draw(VIEW_ERROR, 0);
    usb_mdelay(1500);
    switch_reset(0, BOOT_LOADER);
}
