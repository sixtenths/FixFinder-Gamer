#ifndef FIXFINDER_SWITCH_PACKAGE_H
#define FIXFINDER_SWITCH_PACKAGE_H

#include "switcher.h"
#include "../doom-swd/storage/ff.h"

extern void fixfinder_emmc_probe(void);
extern int fixfinder_library_write_allowed(void);
extern void fixfinder_library_enable_writes(bool enable);
extern void fixfinder_library_status(unsigned int stage, unsigned int count);

struct switch_paths { const char *current, *temporary, *backup; };

static inline struct switch_paths switch_paths(uint32_t game)
{
    if (game == SWITCH_SONIC)
        return (struct switch_paths){"0:/FFSONIC.BIN", "0:/FFSONIC.TMP", "0:/FFSONIC.BAK"};
    return (struct switch_paths){"0:/FFARCAD.BIN", "0:/FFARCAD.TMP", "0:/FFARCAD.BAK"};
}

static inline bool switch_read(FIL *file, void *data, UINT size)
{
    UINT got;
    return f_read(file, data, size, &got) == FR_OK && got == size;
}

static inline bool switch_open(FIL *file, const char *path, uint32_t game,
                               struct switch_image *image)
{
    uint32_t vectors[2];
    if (f_open(file, path, FA_READ) != FR_OK) return false;
    if (switch_read(file, image, sizeof *image) && switch_image_ok(image, game) &&
        f_size(file) == sizeof *image + image->size &&
        switch_read(file, vectors, sizeof vectors) &&
        switch_vectors_ok(vectors[0], vectors[1], image->size) &&
        f_lseek(file, sizeof *image) == FR_OK)
        return true;
    f_close(file);
    return false;
}

#endif
