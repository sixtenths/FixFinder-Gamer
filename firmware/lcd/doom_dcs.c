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
    /* Keep the conservative read timing used to identify the panel.
       Use a shorter data phase for writes, still longer than the stock setting. */
    EXMC_SNWTCFG2 = 0x00000f15U;
    EXMC_SNCTL2 |= EXMC_SNCTL_EXMODEN;
    __DSB();
    start_backlight();
    return true;
}

static void write_pixels(const uint16_t *pixels, unsigned int count)
{
    for (unsigned int x = 0; x < count; ++x) {
        write_data((uint8_t)(pixels[x] >> 8));
        write_data((uint8_t)pixels[x]);
    }
}

static bool write_row(const void *context, unsigned int row, const uint16_t *pixels)
{
    (void)context;
    write_pixels(pixels, DOOM_LCD_WIDTH);
    doom_lcd_write_row(row, pixels);
    return true;
}

void doom_dcs_begin_native_span(unsigned int x, unsigned int width)
{
    begin_panel_window(x, 0, width, DOOM_LCD_HEIGHT);
}

void doom_dcs_write_native_span(unsigned int row, const uint8_t *indices,
                                unsigned int x, unsigned int width)
{
    const uint16_t *pixels = doom_display_native_row(indices);
    write_pixels(pixels + x, width);
    doom_lcd_write_row(row, pixels);
}

void doom_dcs_write_native_dirty_row(unsigned int row, const uint8_t *indices,
                                     unsigned int x, unsigned int width)
{
    begin_panel_window(x, row, width, 1);
    doom_dcs_write_native_span(row, indices, x, width);
}

void doom_dcs_present(const uint8_t *frame)
{
    begin_panel_frame(DOOM_LCD_WIDTH, DOOM_LCD_HEIGHT);
    doom_display_stream(frame, write_row, NULL);
}

void doom_dcs_begin_native(void)
{
    begin_panel_frame(DOOM_LCD_WIDTH, DOOM_LCD_HEIGHT);
}

void doom_dcs_write_native(unsigned int row, const uint8_t *indices)
{
    write_row(NULL, row, doom_display_native_row(indices));
}
