#ifndef FIXFINDER_DOOM_SAVES_H
#define FIXFINDER_DOOM_SAVES_H
#include <stdbool.h>
#include <stdint.h>

bool fixfinder_doom_save(const uint8_t *data, unsigned int size, unsigned int offset);
void fixfinder_doom_load(uint8_t *data, unsigned int size, unsigned int offset);
#endif
