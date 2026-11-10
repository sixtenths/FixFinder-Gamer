#ifndef FIXFINDER_ARCADE_H
#define FIXFINDER_ARCADE_H

#include <stdbool.h>
#include <stdint.h>
#include "../lcd/fixfinder_io.h"

bool fixfinder_arcade_exit(uint32_t keys, uint32_t now);
static inline bool fixfinder_arcade_suspend(uint32_t keys, uint32_t now)
{
#ifdef FIXFINDER_SAVE_FILES
    (void)now;
    return keys == (KEY_DOWN | KEY_BACK);
#else
    return fixfinder_arcade_exit(keys, now);
#endif
}

void fixfinder_arcade_run(uint8_t *frame, const volatile uint32_t *clock);

#endif
