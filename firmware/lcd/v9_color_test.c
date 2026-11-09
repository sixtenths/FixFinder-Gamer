#include "v9_color_test.h"

volatile struct v9_lcd_progress v9_lcd_progress;

bool v9_lcd_color_test(const struct hx8347_bus *bus, uint8_t expected_id,
                      bool landscape, void (*delay_ms)(uint32_t))
{
#ifdef FIXFINDER_LCD_SOLID_GREEN
    static const uint16_t colors[] = {0x07e0};
#else
    static const uint16_t colors[] = {0xf800, 0x07e0, 0x001f, 0xffff, 0x0000};
#endif
    uint8_t id;

    v9_lcd_progress.controller_id = 0;
    v9_lcd_progress.color = 0;
    v9_lcd_progress.phase = V9_LCD_READING_ID;
    if (!hx8347_read_id(bus, &id)) {
        v9_lcd_progress.phase = V9_LCD_ID_UNREADABLE;
        return false;
    }
    v9_lcd_progress.controller_id = id;
    if (id != expected_id) {
        v9_lcd_progress.phase = V9_LCD_ID_MISMATCH;
        return false;
    }

    for (size_t i = 0; i < sizeof colors / sizeof colors[0]; i++) {
        v9_lcd_progress.color = colors[i];
        v9_lcd_progress.phase = V9_LCD_SHOWING_COLOR;
        if (!hx8347_fill_rgb565(bus, landscape, colors[i])) {
            v9_lcd_progress.phase = V9_LCD_FILL_FAILED;
            return false;
        }
        delay_ms(1000);
    }
    v9_lcd_progress.phase = V9_LCD_COMPLETE;
    return true;
}
