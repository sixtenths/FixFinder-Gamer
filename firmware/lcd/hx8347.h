#ifndef FIXFINDER_HX8347_H
#define FIXFINDER_HX8347_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The board transport owns lane mapping, bus direction, strobes and timing.
 * A failed begin_pixels leaves the bus idle. write_pixels consumes the
 * supplied buffer before returning. end_pixels returns the bus to idle. */
struct hx8347_bus {
    void *context;
    bool (*read_register)(void *context, uint8_t index, uint8_t *value);
    bool (*write_register)(void *context, uint8_t index, uint8_t value);
    bool (*begin_pixels)(void *context, uint8_t index);
    bool (*write_pixels)(void *context, const uint16_t *pixels, size_t count);
    void (*end_pixels)(void *context);
};

/* The controller must be on page 0 in register-content mode. This reports
 * a repeatable raw value, which alone does not identify the exact variant. */
bool hx8347_read_id(const struct hx8347_bus *bus, uint8_t *id);

/* The board must already have initialized the controller for RGB565 and
 * configured the selected orientation. A successful begin_frame opens a
 * stream that the caller must close with end_pixels. */
bool hx8347_begin_frame(const struct hx8347_bus *bus, bool landscape);
bool hx8347_fill_rgb565(const struct hx8347_bus *bus, bool landscape, uint16_t color);

#endif
