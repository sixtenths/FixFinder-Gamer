#include "../doom-swd/display.h"
#include "exmc_dcs.h"
#include "drv_usb_hw.h"
#include <string.h>

static bool ordered_pixels;

static void configure_lcd_memory(void)
{
    uint32_t regions = (MPU->TYPE & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;
    if (!regions || (MPU->CTRL & MPU_CTRL_ENABLE_Msk)) return;
    /* Map the command/data window as shareable Device memory. The default
     * external-RAM attributes do not guarantee ordered peripheral writes. */
    __DMB();
    MPU->RNR = regions - 1;
    MPU->RBAR = 0x68000000U;
    MPU->RASR = MPU_RASR_XN_Msk | (3U << MPU_RASR_AP_Pos) | MPU_RASR_S_Msk |
                MPU_RASR_B_Msk | (4U << MPU_RASR_SIZE_Pos) | MPU_RASR_ENABLE_Msk;
    MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
    __DSB();
    __ISB();
    ordered_pixels = true;
}

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
    EXMC_SNWTCFG2 = 0x00000b15U;
    EXMC_SNCTL2 |= EXMC_SNCTL_EXMODEN;
    __DSB();
    configure_lcd_memory();
    start_backlight();
    return true;
}

static void write_pixels(const uint16_t *pixels, unsigned int count)
{
    if (ordered_pixels) {
        for (unsigned int x = 0; x < count; ++x) {
            LCD_DATA = (uint8_t)(pixels[x] >> 8);
            LCD_DATA = (uint8_t)pixels[x];
        }
        __DSB();
    } else {
        for (unsigned int x = 0; x < count; ++x) {
            write_data((uint8_t)(pixels[x] >> 8));
            write_data((uint8_t)pixels[x]);
        }
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
    write_pixels(doom_display_native_pixels(indices + x, width), width);
}

void doom_dcs_update_native_row(unsigned int row, const uint8_t *indices,
                                uint8_t *previous, unsigned int x, unsigned int width)
{
    const uint8_t *current = indices + x;
    if (memcmp(previous, current, width) == 0) return;
    unsigned int first = 0, end = width;
    while (previous[first] == current[first]) ++first;
    while (previous[end - 1] == current[end - 1]) --end;
    doom_dcs_write_native_dirty_row(row, indices, x + first, end - first);
    memcpy(previous + first, current + first, end - first);
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

void doom_dcs_begin_native_rows(unsigned int first)
{
    begin_panel_window(0, first, DOOM_LCD_WIDTH, DOOM_LCD_HEIGHT - first);
}

void doom_dcs_write_native(unsigned int row, const uint8_t *indices)
{
    write_row(NULL, row, doom_display_native_row(indices));
}
