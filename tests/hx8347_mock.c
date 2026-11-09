#include "hx8347.h"
#include "display.h"
#include "v9_color_test.h"

static const uint8_t portrait_window[] = {0, 0, 0, 239, 0, 0, 1, 63};
static const uint8_t landscape_window[] = {0, 0, 1, 63, 0, 0, 0, 239};
static const uint16_t pattern_colors[] = {0xf800, 0x07e0, 0x001f};

static struct {
    uint8_t reads[2];
    unsigned int read_count;
    const uint8_t *window;
    unsigned int register_count;
    size_t width, height, rows, fail_row;
    uint16_t color;
    bool streaming, ended, unexpected, pattern;
} mock;

void doom_lcd_write_row(unsigned int row, const uint16_t *pixels)
{
    (void)row;
    (void)pixels;
    mock.unexpected = true;
}

static bool read_register(void *context, uint8_t index, uint8_t *value)
{
    (void)context;
    if (index != 0 || mock.read_count >= 2) {
        return false;
    }
    *value = mock.reads[mock.read_count++];
    return true;
}

static bool write_register(void *context, uint8_t index, uint8_t value)
{
    (void)context;
    const unsigned int i = mock.register_count++;
    return i < 8 && index == i + 2 && value == mock.window[i];
}

static bool begin_pixels(void *context, uint8_t index)
{
    (void)context;
    mock.streaming = index == 0x22 && mock.register_count == 8;
    return mock.streaming;
}

static bool write_pixels(void *context, const uint16_t *pixels, size_t count)
{
    (void)context;
    if (!mock.streaming || count != mock.width || mock.rows >= mock.height) {
        mock.unexpected = true;
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        const size_t pattern = (i * 3 / 4 + mock.rows * 2 / 3) % 3;
        const uint16_t expected = mock.pattern ? pattern_colors[pattern] : mock.color;
        if (pixels[i] != expected) {
            mock.unexpected = true;
            return false;
        }
    }
    return ++mock.rows != mock.fail_row;
}

static void end_pixels(void *context)
{
    (void)context;
    mock.ended = mock.streaming;
    mock.streaming = false;
}

static const struct hx8347_bus bus = {
    .read_register = read_register,
    .write_register = write_register,
    .begin_pixels = begin_pixels,
    .write_pixels = write_pixels,
    .end_pixels = end_pixels
};

static void setup_frame(bool landscape, size_t fail_row)
{
    mock.window = landscape ? landscape_window : portrait_window;
    mock.width = landscape ? 320 : 240;
    mock.height = landscape ? 240 : 320;
    mock.rows = mock.register_count = 0;
    mock.fail_row = fail_row;
    mock.streaming = mock.ended = mock.unexpected = mock.pattern = false;
}

static bool check_frame(bool sent)
{
    return sent == (mock.fail_row == 0) && mock.ended && !mock.streaming &&
           !mock.unexpected && mock.rows == (mock.fail_row ? mock.fail_row : mock.height);
}

static bool check_fill(bool landscape, uint16_t color, size_t fail_row)
{
    setup_frame(landscape, fail_row);
    mock.color = color;
    return check_frame(hx8347_fill_rgb565(&bus, landscape, color));
}

static bool check_game_frame(size_t fail_row)
{
    static const uint8_t rgb[256 * 3] = {255, 0, 0, 0, 255, 0, 0, 0, 255};
    static uint8_t indexed[DOOM_FRAME_HEIGHT * DOOM_FRAME_WIDTH];
    for (unsigned int y = 0; y < DOOM_FRAME_HEIGHT; y++) {
        for (unsigned int x = 0; x < DOOM_FRAME_WIDTH; x++) {
            indexed[y * DOOM_FRAME_WIDTH + x] = (uint8_t)((x + y) % 3);
        }
    }
    doom_display_set_palette(rgb);
    setup_frame(true, fail_row);
    mock.pattern = true;
    return check_frame(doom_display_hx8347(indexed, &bus));
}

static const uint16_t diagnostic_colors[] = {0xf800, 0x07e0, 0x001f, 0xffff, 0x0000};
static size_t delays;
static bool bad_delay;

static void check_color_delay(uint32_t milliseconds)
{
    if (milliseconds != 1000 || delays >= 5 || !check_frame(true) ||
        v9_lcd_progress.phase != V9_LCD_SHOWING_COLOR ||
        v9_lcd_progress.color != diagnostic_colors[delays]) {
        bad_delay = true;
        return;
    }
    if (++delays < 5) {
        setup_frame(true, 0);
        mock.color = diagnostic_colors[delays];
    }
}

static int check_v9(void)
{
    setup_frame(true, 0);
    mock.read_count = 0;
    if (v9_lcd_color_test(&bus, 0x47, true, check_color_delay) ||
        v9_lcd_progress.phase != V9_LCD_ID_UNREADABLE || mock.register_count != 0) return 10;

    mock.read_count = 0;
    mock.reads[0] = mock.reads[1] = 0x75;
    if (v9_lcd_color_test(&bus, 0x47, true, check_color_delay) ||
        v9_lcd_progress.phase != V9_LCD_ID_MISMATCH ||
        v9_lcd_progress.controller_id != 0x75 || mock.register_count != 0) return 11;

    mock.read_count = 0;
    mock.reads[0] = mock.reads[1] = 0x47;
    setup_frame(true, 4);
    mock.color = diagnostic_colors[0];
    if (v9_lcd_color_test(&bus, 0x47, true, check_color_delay) ||
        v9_lcd_progress.phase != V9_LCD_FILL_FAILED ||
        v9_lcd_progress.color != diagnostic_colors[0] || !check_frame(false) ||
        delays || bad_delay) return 12;

    mock.read_count = 0;
    setup_frame(true, 0);
    mock.color = diagnostic_colors[0];
    if (!v9_lcd_color_test(&bus, 0x47, true, check_color_delay) ||
        v9_lcd_progress.phase != V9_LCD_COMPLETE ||
        v9_lcd_progress.controller_id != 0x47 || delays != 5 || bad_delay) return 13;
    return 0;
}

int test_lcd(void)
{
    uint8_t id = 0;
    mock.reads[0] = mock.reads[1] = 0x47;
    if (!hx8347_read_id(&bus, &id) || id != 0x47) return 1;
    mock.read_count = 0;
    mock.reads[1] = 0x75;
    if (hx8347_read_id(&bus, &id) || id != 0x47) return 2;
    mock.read_count = 0;
    mock.reads[0] = mock.reads[1] = 0xff;
    if (hx8347_read_id(&bus, &id)) return 3;
    mock.read_count = 0;
    mock.reads[0] = mock.reads[1] = 0;
    if (hx8347_read_id(&bus, &id)) return 4;
    if (!check_fill(false, 0xf800, 0)) return 5;
    if (!check_fill(true, 0x07e0, 0)) return 6;
    if (!check_fill(true, 0x001f, 4)) return 7;
    if (!check_game_frame(0)) return 8;
    if (!check_game_frame(4)) return 9;
    return check_v9();
}
