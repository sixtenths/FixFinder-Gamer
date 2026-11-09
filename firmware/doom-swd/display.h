#ifndef FIXFINDER_DOOM_DISPLAY_H
#define FIXFINDER_DOOM_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

enum {
    DOOM_FRAME_WIDTH = 240,
    DOOM_FRAME_HEIGHT = 160,
    DOOM_LCD_WIDTH = 320,
    DOOM_LCD_HEIGHT = 240
};

void doom_display_set_palette(const uint8_t *rgb);
void doom_display_present(const uint8_t *frame);
const uint16_t *doom_display_native_row(const uint8_t *indices);
void doom_dcs_begin_native(void);
void doom_dcs_write_native(unsigned int row, const uint8_t *indices);
bool doom_display_stream(const uint8_t *frame,
                         bool (*write_row)(const void *context, unsigned int row,
                                           const uint16_t *pixels),
                         const void *context);

struct hx8347_bus;
/* The caller supplies an initialized RGB565 landscape transport. */
bool doom_display_hx8347(const uint8_t *frame, const struct hx8347_bus *bus,
                        void (*observe_row)(unsigned int row, const uint16_t *pixels));

/* Consume one logical landscape row before returning. The buffer is reused. */
void doom_lcd_write_row(unsigned int row, const uint16_t *pixels);

#endif
