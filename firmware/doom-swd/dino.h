#ifndef FIXFINDER_DINO_H
#define FIXFINDER_DINO_H

#include <stdbool.h>
#include <stdint.h>

bool fixfinder_dino_run(const volatile uint32_t *clock, unsigned int *score);

#endif
