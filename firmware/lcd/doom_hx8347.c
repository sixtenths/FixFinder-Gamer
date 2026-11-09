#include "../doom-swd/display.h"
#include "hx8347.h"

struct frame_transfer {
    const struct hx8347_bus *bus;
    void (*observe_row)(unsigned int row, const uint16_t *pixels);
};

static bool write_row(const void *context, unsigned int row, const uint16_t *pixels)
{
    const struct frame_transfer *transfer = context;
    const struct hx8347_bus *bus = transfer->bus;
    if (!bus->write_pixels(bus->context, pixels, DOOM_LCD_WIDTH)) {
        return false;
    }
    if (transfer->observe_row != NULL) {
        transfer->observe_row(row, pixels);
    }
    return true;
}

bool doom_display_hx8347(const uint8_t *frame, const struct hx8347_bus *bus,
                        void (*observe_row)(unsigned int row, const uint16_t *pixels))
{
    if (!hx8347_begin_frame(bus, true)) {
        return false;
    }
    const struct frame_transfer transfer = {bus, observe_row};
    const bool sent = doom_display_stream(frame, write_row, &transfer);
    bus->end_pixels(bus->context);
    return sent;
}
