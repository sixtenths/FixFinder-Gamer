#ifndef FIXFINDER_LIBRARY_H
#define FIXFINDER_LIBRARY_H
#include <stdbool.h>
#include <stdint.h>
#define PACMAN_ROM_SIZE (0x6000U + 32U + 256U)
bool fixfinder_library_read_pacman(uint8_t *buffer, unsigned int size);
unsigned int fixfinder_library_init(void);
const char *fixfinder_library_title(unsigned int index);
bool fixfinder_library_play(unsigned int index, uint8_t *frame, const volatile uint32_t *clock);
#endif
