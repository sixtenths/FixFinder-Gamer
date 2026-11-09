#include "hx8347.h"

static struct {
    uint8_t reads[2];
    unsigned int read_count;
    const uint8_t *window;
    unsigned int register_count;
    size_t width, height, rows, fail_row;
    uint16_t color;
    bool streaming, ended, unexpected;
} mock;

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
        if (pixels[i] != mock.color) {
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

static bool check_fill(bool landscape, uint16_t color, size_t fail_row)
{
    static const uint8_t portrait_window[] = {0, 0, 0, 239, 0, 0, 1, 63};
    static const uint8_t landscape_window[] = {0, 0, 1, 63, 0, 0, 0, 239};
    mock.window = landscape ? landscape_window : portrait_window;
    mock.width = landscape ? 320 : 240;
    mock.height = landscape ? 240 : 320;
    mock.rows = mock.register_count = 0;
    mock.color = color;
    mock.fail_row = fail_row;
    mock.streaming = mock.ended = mock.unexpected = false;
    const bool filled = hx8347_fill_rgb565(&bus, landscape, color);
    return filled == (fail_row == 0) && mock.ended && !mock.streaming &&
           !mock.unexpected && mock.rows == (fail_row ? fail_row : mock.height);
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
    return 0;
}
