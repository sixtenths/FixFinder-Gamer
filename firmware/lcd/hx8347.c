#include "hx8347.h"

enum {
    HX8347_ID = 0x00,
    HX8347_COLUMN_START = 0x02,
    HX8347_COLUMN_END = 0x04,
    HX8347_ROW_START = 0x06,
    HX8347_ROW_END = 0x08,
    HX8347_GRAM = 0x22,
    HX8347_SHORT_EDGE = 240,
    HX8347_LONG_EDGE = 320
};

bool hx8347_read_id(const struct hx8347_bus *bus, uint8_t *id)
{
    uint8_t first, second;
    if (!bus->read_register(bus->context, HX8347_ID, &first) ||
        !bus->read_register(bus->context, HX8347_ID, &second) ||
        first != second || first == 0 || first == UINT8_MAX) {
        return false;
    }
    *id = first;
    return true;
}

static bool write_address(const struct hx8347_bus *bus, uint8_t index, uint16_t address)
{
    return bus->write_register(bus->context, index, (uint8_t)(address >> 8)) &&
           bus->write_register(bus->context, index + 1U, (uint8_t)address);
}

bool hx8347_begin_frame(const struct hx8347_bus *bus, bool landscape)
{
    const uint16_t width = landscape ? HX8347_LONG_EDGE : HX8347_SHORT_EDGE;
    const uint16_t height = landscape ? HX8347_SHORT_EDGE : HX8347_LONG_EDGE;
    return write_address(bus, HX8347_COLUMN_START, 0) &&
           write_address(bus, HX8347_COLUMN_END, width - 1U) &&
           write_address(bus, HX8347_ROW_START, 0) &&
           write_address(bus, HX8347_ROW_END, height - 1U) &&
           bus->begin_pixels(bus->context, HX8347_GRAM);
}

bool hx8347_fill_rgb565(const struct hx8347_bus *bus, bool landscape, uint16_t color)
{
    uint16_t row[HX8347_LONG_EDGE];
    const size_t width = landscape ? HX8347_LONG_EDGE : HX8347_SHORT_EDGE;
    const size_t height = landscape ? HX8347_SHORT_EDGE : HX8347_LONG_EDGE;
    if (!hx8347_begin_frame(bus, landscape)) {
        return false;
    }
    for (size_t x = 0; x < width; x++) {
        row[x] = color;
    }
    for (size_t y = 0; y < height; y++) {
        if (!bus->write_pixels(bus->context, row, width)) {
            bus->end_pixels(bus->context);
            return false;
        }
    }
    bus->end_pixels(bus->context);
    return true;
}
