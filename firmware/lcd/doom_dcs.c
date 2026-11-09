#include "../doom-swd/display.h"
#include "exmc_dcs.h"
#include "drv_usb_hw.h"

bool doom_dcs_init(void)
{
    init_display_bus();
    usb_mdelay(150);
    if (!panel_matches()) {
        return false;
    }
    start_panel(usb_mdelay, 0xa0);  /* Upright landscape in the FixFinder case. */
    start_backlight();
    return true;
}

static bool write_row(const void *context, unsigned int row, const uint16_t *pixels)
{
    (void)context;
    for (unsigned int x = 0; x < DOOM_LCD_WIDTH; ++x) {
        write_data((uint8_t)(pixels[x] >> 8));
        write_data((uint8_t)pixels[x]);
    }
    doom_lcd_write_row(row, pixels);
    return true;
}

void doom_dcs_present(const uint8_t *frame)
{
    begin_panel_frame(DOOM_LCD_WIDTH, DOOM_LCD_HEIGHT);
    doom_display_stream(frame, write_row, NULL);
}
