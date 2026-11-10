#include "package.h"
#include "../game-sdk/fixfinder.h"
#include "../doom-swd/usb_video.h"
#include <string.h>

extern const uint8_t switch_seed_start[], switch_seed_end[];
static FATFS storage;
static uint8_t block[1024];
static unsigned int failed_step;
static const uint8_t palette[FIXFINDER_PALETTE_SIZE] = {
    17, 22, 30, 44, 51, 62, 255, 127, 32, 41, 201, 112, 245, 60, 72
};

static void progress(unsigned int percent, uint8_t color)
{
    unsigned int done = 200 * percent / 100;
    for (unsigned int y = 77; y < 87; ++y) {
        uint8_t *line = fixfinder_screen + y * FIXFINDER_WIDTH + 20;
        memset(line, color, done);
        memset(line + done, 1, 200 - done);
    }
    fixfinder_present();
}

static bool matches(FIL *file, unsigned int size)
{
    if (f_size(file) != size) return false;
    for (unsigned int offset = 0; offset < size;) {
        unsigned int count = size - offset < sizeof block ? size - offset : sizeof block;
        if (!switch_read(file, block, count) || memcmp(block, switch_seed_start + offset, count))
            return false;
        offset += count;
    }
    return true;
}

static bool file_matches(const char *path, unsigned int size)
{
    FIL file;
    if (f_open(&file, path, FA_READ) != FR_OK) return false;
    bool ok = matches(&file, size);
    return f_close(&file) == FR_OK && ok;
}

static bool provision(const struct switch_image *image, unsigned int size)
{
    struct switch_paths paths = switch_paths(image->game);
    if (file_matches(paths.current, size)) return true;

    fixfinder_library_enable_writes(true);
    FIL file;
    bool ok = false;
    failed_step = 3;
    FRESULT removed = f_unlink(paths.temporary);
    if ((removed != FR_OK && removed != FR_NO_FILE) ||
        f_open(&file, paths.temporary, FA_WRITE | FA_CREATE_NEW) != FR_OK)
        goto done;
    ok = true;
    failed_step = 4;
    unsigned int last_progress = 0;
    for (unsigned int offset = 0; ok && offset < size;) {
        unsigned int count = size - offset < sizeof block ? size - offset : sizeof block;
        UINT written;
        ok = f_write(&file, switch_seed_start + offset, count, &written) == FR_OK && written == count;
        offset += count;
        unsigned int pct = offset * 10U / size;
        if (pct != last_progress) {
            progress(pct * 9U, 2);
            last_progress = pct;
        }
    }
    if (f_close(&file) != FR_OK) ok = false;
    if (ok) {
        failed_step = 5;
        ok = file_matches(paths.temporary, size);
    }
    if (ok) {
        failed_step = 6;
        FRESULT result = f_rename(paths.temporary, paths.current);
        if (result == FR_EXIST) {
            /* Keep the preceding package available while the new one is moved. */
            removed = f_unlink(paths.backup);
            ok = (removed == FR_OK || removed == FR_NO_FILE) &&
                 f_rename(paths.current, paths.backup) == FR_OK &&
                 f_rename(paths.temporary, paths.current) == FR_OK;
        } else {
            ok = result == FR_OK;
        }
    }
done:
    fixfinder_library_enable_writes(false);
    return ok;
}

void fixfinder_game_main(void)
{
    fixfinder_palette(palette);
    progress(0, 2);
    fixfinder_set_led(FIXFINDER_LED_YELLOW);
    fixfinder_emmc_probe();
    const struct switch_image *image = (const void *)switch_seed_start;
    const uint32_t *vectors = (const void *)(image + 1);
    unsigned int size = (uintptr_t)switch_seed_end - (uintptr_t)switch_seed_start;
    failed_step = 1;
    bool ok = size >= sizeof *image + 8 && switch_game_known(image->game) &&
              switch_image_ok(image, image->game) && image->size + sizeof *image == size &&
              switch_vectors_ok(vectors[0], vectors[1], image->size) &&
              fixfinder_library_write_allowed();
    if (ok) {
        failed_step = 2;
        ok = f_mount(&storage, "0:", 1) == FR_OK && storage.fs_type == FS_FAT32 &&
             provision(image, size);
    }
    fixfinder_library_status(ok ? SWITCH_SEED_STATUS : SWITCH_SEED_ERROR | failed_step, image->crc);
    fixfinder_set_led(ok ? FIXFINDER_LED_GREEN : FIXFINDER_LED_RED);
    progress(100, ok ? 3 : 4);
    for (;;) {
        doom_usb_video_capture(fixfinder_screen, palette);
        fixfinder_sleep(1);
    }
}
