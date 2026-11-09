#ifndef FIXFINDER_DOOM_DISPLAY_H
#define FIXFINDER_DOOM_DISPLAY_H

#include <stdint.h>

enum {
    DOOM_FRAME_WIDTH = 240,
    DOOM_FRAME_HEIGHT = 160,
    DOOM_LCD_WIDTH = 320,
    DOOM_LCD_HEIGHT = 240
};

void doom_display_set_palette(const uint8_t *rgb);
void doom_display_present(const uint8_t *frame);

/* Consume one logical landscape row before returning. The buffer is reused. */
void doom_lcd_write_row(unsigned int row, const uint16_t *pixels);

#endif
