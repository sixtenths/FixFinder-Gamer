#ifndef FIXFINDER_GAME_SDK_H
#define FIXFINDER_GAME_SDK_H

#include "../lcd/fixfinder_io.h"

enum { FIXFINDER_WIDTH = 240, FIXFINDER_HEIGHT = 160, FIXFINDER_PALETTE_SIZE = 768 };

/* Row-major palette indices. The board scales this buffer to the 320 x 240 LCD. */
extern uint8_t fixfinder_screen[FIXFINDER_WIDTH * FIXFINDER_HEIGHT];

uint32_t fixfinder_millis(void);
void fixfinder_sleep(uint32_t ms);
void fixfinder_palette(const uint8_t rgb[FIXFINDER_PALETTE_SIZE]);
void fixfinder_present(void);

/* Implement this entry point in a custom game. Hardware is already initialized. */
void fixfinder_game_main(void);

#endif
