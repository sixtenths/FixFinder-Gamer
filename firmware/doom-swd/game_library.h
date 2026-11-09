#ifndef FIXFINDER_LIBRARY_H
#define FIXFINDER_LIBRARY_H
#include <stdbool.h>
#include <stdint.h>
unsigned int fixfinder_library_init(void);
const char *fixfinder_library_title(unsigned int index);
bool fixfinder_library_play(unsigned int index, uint8_t *frame, const volatile uint32_t *clock);
#endif
