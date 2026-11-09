#ifndef FIXFINDER_ARCADE_H
#define FIXFINDER_ARCADE_H

#include <stdbool.h>
#include <stdint.h>

bool fixfinder_arcade_exit(uint32_t keys, uint32_t now);
void fixfinder_arcade_run(uint8_t *frame, const volatile uint32_t *clock);

#endif
