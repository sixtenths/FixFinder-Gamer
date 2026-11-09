#include "display.h"

static uint16_t palette[256];
/* Kept in AHB SRAM so a display backend can transfer the row with DMA. */
static uint16_t scanline[DOOM_LCD_WIDTH] __attribute__((aligned(4)));

void doom_display_set_palette(const uint8_t *rgb)
{
    for (unsigned int i = 0; i < 256; i++, rgb += 3) {
        palette[i] = (uint16_t)(((rgb[0] & 0xf8U) << 8) |
                               ((rgb[1] & 0xfcU) << 3) |
                               (rgb[2] >> 3));
    }
}

bool doom_display_stream(const uint8_t *frame,
                         bool (*write_row)(const void *, unsigned int, const uint16_t *),
                         const void *context)
{
    unsigned int previous_source_y = DOOM_FRAME_HEIGHT;
    for (unsigned int y = 0; y < DOOM_LCD_HEIGHT; y++) {
        const unsigned int source_y = y * DOOM_FRAME_HEIGHT / DOOM_LCD_HEIGHT;
        if (source_y != previous_source_y) {
            const uint8_t *source = frame + source_y * DOOM_FRAME_WIDTH;
            for (unsigned int x = 0; x < DOOM_LCD_WIDTH; x++) {
                scanline[x] = palette[source[x * DOOM_FRAME_WIDTH / DOOM_LCD_WIDTH]];
            }
            previous_source_y = source_y;
        }
        if (!write_row(context, y, scanline)) {
            return false;
        }
    }
    return true;
}

static bool write_diagnostic_row(const void *context, unsigned int row, const uint16_t *pixels)
{
    (void)context;
    doom_lcd_write_row(row, pixels);
    return true;
}

void doom_display_present(const uint8_t *frame)
{
    doom_display_stream(frame, write_diagnostic_row, 0);
}
