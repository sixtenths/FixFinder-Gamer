#ifndef FIXFINDER_V9_COLOR_TEST_H
#define FIXFINDER_V9_COLOR_TEST_H

#include "hx8347.h"

enum v9_lcd_phase {
    V9_LCD_IDLE,
    V9_LCD_READING_ID,
    V9_LCD_ID_UNREADABLE,
    V9_LCD_ID_MISMATCH,
    V9_LCD_SHOWING_COLOR,
    V9_LCD_FILL_FAILED,
    V9_LCD_COMPLETE
};

struct v9_lcd_progress {
    uint32_t phase;
    uint32_t controller_id;
    uint32_t color;
};

extern volatile struct v9_lcd_progress v9_lcd_progress;

/* Requires a verified, initialized HX8347 transport and panel orientation.
 * The ID check corroborates the expected controller; it does not identify it.
 * Runs one cycle and leaves the final color visible. */
bool v9_lcd_color_test(const struct hx8347_bus *bus, uint8_t expected_id,
                      bool landscape, void (*delay_ms)(uint32_t));

#endif
