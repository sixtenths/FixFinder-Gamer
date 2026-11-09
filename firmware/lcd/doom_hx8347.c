#include "../doom-swd/display.h"
#include "hx8347.h"

static bool write_row(const void *context, unsigned int row, const uint16_t *pixels)
{
    const struct hx8347_bus *bus = context;
    (void)row;
    return bus->write_pixels(bus->context, pixels, DOOM_LCD_WIDTH);
}

bool doom_display_hx8347(const uint8_t *frame, const struct hx8347_bus *bus)
{
    if (!hx8347_begin_frame(bus, true)) {
        return false;
    }
    const bool sent = doom_display_stream(frame, write_row, bus);
    bus->end_pixels(bus->context);
    return sent;
}
