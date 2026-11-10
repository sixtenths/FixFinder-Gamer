#include "save_files.h"
#include "storage/ff.h"
#include <string.h>

#define SAVE_MAGIC UINT32_C(0x31534646) /* FFS1 */
enum { MAX_SAVE_BYTES = 4096 };

struct save_header {
    uint32_t magic, size, sequence, crc;
};

extern int fixfinder_library_write_allowed(void);
extern void fixfinder_library_enable_writes(bool enable);
const char *fixfinder_save_notice;

static void save_path(char path[16], const char *name, unsigned int copy)
{
    strcpy(path, "0:/");
    strcat(path, name);
    strcat(path, copy ? ".B" : ".A");
}

static uint32_t crc_bytes(uint32_t crc, const void *data, unsigned int size)
{
    const uint8_t *bytes = data;
    while (size--) {
        crc ^= *bytes++;
        for (unsigned int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc;
}

enum inspect_result { SAVE_INVALID, SAVE_VALID, SAVE_IO_ERROR };

static enum inspect_result inspect(const char *path, const char *name, void *dest, unsigned int size, uint32_t *sequence)
{
    FIL file;
    FRESULT result = f_open(&file, path, FA_READ);
    if (result == FR_NO_FILE) return SAVE_INVALID;
    if (result != FR_OK) return SAVE_IO_ERROR;

    struct save_header header;
    UINT got;
    bool ok = f_size(&file) == sizeof header + size;
    if (ok) {
        result = f_read(&file, &header, sizeof header, &got);
        ok = result == FR_OK && got == sizeof header &&
             header.magic == SAVE_MAGIC && header.size == size;
    }
    if (ok) {
        uint8_t scratch[128];
        uint32_t crc = crc_bytes(UINT32_MAX, name, strlen(name));
        crc = crc_bytes(crc, &header.sequence, sizeof header.sequence);
        for (unsigned int offset = 0; ok && offset < size;) {
            unsigned int count = size - offset;
            if (count > sizeof scratch) count = sizeof scratch;
            result = f_read(&file, scratch, count, &got);
            ok = result == FR_OK && got == count;
            if (ok) {
                crc = crc_bytes(crc, scratch, count);
                if (dest) memcpy((uint8_t *)dest + offset, scratch, count);
            }
            offset += count;
        }
        ok = ok && ~crc == header.crc;
    }
    if (f_close(&file) != FR_OK || result != FR_OK) return SAVE_IO_ERROR;
    if (!ok) return SAVE_INVALID;
    *sequence = header.sequence;
    return SAVE_VALID;
}

/* All callers run on the game thread. Never overwrite the newest valid copy. */
static int newest_copy(char paths[2][16], const char *name, unsigned int size,
                       uint32_t *sequence, bool writing)
{
    uint32_t a, b;
    save_path(paths[0], name, 0);
    save_path(paths[1], name, 1);
    enum inspect_result first = inspect(paths[0], name, NULL, size, &a);
    enum inspect_result second = inspect(paths[1], name, NULL, size, &b);
    if (writing && (first == SAVE_IO_ERROR || second == SAVE_IO_ERROR)) return -2;
    bool valid_a = first == SAVE_VALID, valid_b = second == SAVE_VALID;
    if (!valid_a && !valid_b) return -1;
    if (valid_a && (!valid_b || (int32_t)(a - b) >= 0)) {
        *sequence = a;
        return 0;
    }
    *sequence = b;
    return 1;
}

bool fixfinder_save_read(const char *name, void *data, unsigned int size)
{
    if (!size || size > MAX_SAVE_BYTES || strlen(name) > 8) return false;
    char paths[2][16];
    uint32_t sequence;
    int copy = newest_copy(paths, name, size, &sequence, false);
    if (copy < 0) return false;

    uint8_t payload[size];
    if (inspect(paths[copy], name, payload, size, &sequence) != SAVE_VALID &&
        inspect(paths[copy ^ 1], name, payload, size, &sequence) != SAVE_VALID) return false;
    memcpy(data, payload, size);
    return true;
}

bool fixfinder_save_write(const char *name, const void *data, unsigned int size)
{
    fixfinder_save_notice = "Save failed";
    if (!size || size > MAX_SAVE_BYTES || strlen(name) > 8 ||
        !fixfinder_library_write_allowed()) return false;

    char paths[2][16];
    uint32_t sequence = 0;
    int newest = newest_copy(paths, name, size, &sequence, true);
    if (newest == -2) return false;
    const char *path = paths[newest == 0];
    struct save_header header = {SAVE_MAGIC, size, sequence + 1, 0};
    uint32_t crc = crc_bytes(UINT32_MAX, name, strlen(name));
    crc = crc_bytes(crc, &header.sequence, sizeof header.sequence);
    header.crc = ~crc_bytes(crc, data, size);

    FIL file;
    UINT written;
    bool ok = false;
    fixfinder_library_enable_writes(true);
    if (f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {
        ok = f_write(&file, &header, sizeof header, &written) == FR_OK && written == sizeof header &&
             f_write(&file, data, size, &written) == FR_OK && written == size;
        if (f_close(&file) != FR_OK) ok = false;
    }
    fixfinder_library_enable_writes(false);
    ok = ok && inspect(path, name, NULL, size, &sequence) == SAVE_VALID && sequence == header.sequence;
    if (ok) fixfinder_save_notice = "Progress saved";
    return ok;
}
