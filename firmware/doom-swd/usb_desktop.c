#include "usb_desktop.h"
#include "usb_video.h"
#include "display.h"
#include "../lcd/fixfinder_io.h"
#include "custom_hid_core.h"
#include <string.h>

enum { START = 1, TILE, DATA, STOP, PING, BATCH };
enum { RAW, RUNS, INDEXED, MONO, FOUR_COLORS };
enum { TILE_W = 40, TILE_H = 20, PIXELS = TILE_W * TILE_H,
       COLUMNS = DOOM_LCD_WIDTH / TILE_W, ROWS = DOOM_LCD_HEIGHT / TILE_H };

extern usb_core_driver custom_hid;
static uint16_t pixels[PIXELS] __attribute__((section(".doom_video")));
static uint8_t reply[DESKTOP_REPORT_SIZE] __attribute__((aligned(4)));
static volatile bool running, reply_pending, tile_ready, batch_ready, batch_receiving;
static volatile uint8_t request;
static uint8_t tile, encoding;
static uint16_t expected, received, sequence;
static bool invalid;

static uint16_t le16(const uint8_t *bytes)
{
    return bytes[0] | (uint16_t)bytes[1] << 8;
}

bool desktop_usb_active(void)
{
    return running || reply_pending;
}

void desktop_usb_reset(void)
{
    running = reply_pending = tile_ready = batch_ready = batch_receiving = false;
    request = 0;
}

bool desktop_usb_sent(void)
{
    if (!reply_pending) return false;
    reply_pending = false;
    return true;
}

void desktop_usb_receive(const uint8_t *packet)
{
    unsigned int op = packet[1];
    if (op == START && !running && packet[2] == 1) request = START;
    if (op == STOP && request == START && !running) request = 0;
    if (!running) return;

    if (op == STOP || op == PING) {
        request = op;
    } else if (batch_receiving || batch_ready) {
        return;
    } else if (op == TILE && !tile_ready) {
        sequence = le16(packet + 2);
        tile = packet[4];
        encoding = packet[5];
        expected = le16(packet + 6);
        received = 0;
        invalid = tile >= COLUMNS * ROWS || encoding > INDEXED ||
                  !expected || expected > sizeof pixels;
        tile_ready = invalid;
    } else if (op == DATA && expected && !tile_ready && le16(packet + 2) == sequence) {
        unsigned int length = packet[6];
        if (le16(packet + 4) != received || !length || length > DESKTOP_REPORT_SIZE - 7 ||
            length > (unsigned int)(expected - received)) {
            invalid = tile_ready = true;
            return;
        }
        memcpy(doom_usb_video_scratch() + received, packet + 7, length);
        received += length;
        tile_ready = received == expected;
    }
}

uint8_t *desktop_usb_batch_buffer(void)
{
    if (!running || tile_ready || batch_ready || expected) return NULL;
    batch_receiving = true;
    return doom_usb_video_scratch();
}

void desktop_usb_batch_received(void)
{
    batch_ready = running && batch_receiving;
    batch_receiving = false;
}

static bool decode_tile(const uint8_t *data, unsigned int size, unsigned int format)
{
    if (!size) return false;
    if (format == RAW) {
        if (size != sizeof pixels) return false;
        memcpy(pixels, data, sizeof pixels);
    } else if (format == RUNS) {
        if (size % 3) return false;
        unsigned int out = 0;
        for (unsigned int i = 0; i < size; i += 3) {
            unsigned int length = data[i];
            uint16_t color = le16(data + i + 1);
            if (!length || length > PIXELS - out) return false;
            while (length--) pixels[out++] = color;
        }
        if (out != PIXELS) return false;
    } else if (format <= FOUR_COLORS) {
        unsigned int bits = format == INDEXED ? 4 : format == MONO ? 1 : 2;
        unsigned int mask = (1U << bits) - 1U;
        unsigned int count = data[0];
        if (!count || count > mask + 1U || size != 1 + 2 * count + PIXELS * bits / 8)
            return false;
        const uint8_t *indices = data + 1 + 2 * count;
        for (unsigned int i = 0; i < PIXELS; ++i) {
            unsigned int bit = i * bits;
            unsigned int color = (indices[bit / 8] >> (bit % 8)) & mask;
            if (color >= count) return false;
            pixels[i] = le16(data + 1 + 2 * color);
        }
    } else return false;
    return true;
}

static void draw_tile(unsigned int index)
{
    doom_dcs_write_rgb_rect(index % COLUMNS * TILE_W, index / COLUMNS * TILE_H,
                            TILE_W, TILE_H, pixels);
}

static bool draw_batch(const uint8_t *data)
{
    /* Report ID, sequence and used length, followed by tile records and padding. */
    unsigned int end = le16(data + 3);
    if (data[0] != DESKTOP_BATCH_ID || end <= 5 || end > DESKTOP_BATCH_SIZE)
        return false;
    for (unsigned int offset = 5; offset < end;) {
        if (end - offset < 4) return false;
        unsigned int index = data[offset], format = data[offset + 1];
        unsigned int size = le16(data + offset + 2);
        offset += 4;
        if (index >= COLUMNS * ROWS || size > end - offset ||
            !decode_tile(data + offset, size, format))
            return false;
        draw_tile(index);
        offset += size;
    }
    return true;
}

/* Call with interrupts masked after checking that the IN pipe is idle. */
static void acknowledge(uint8_t op, uint8_t status, uint16_t token)
{
    reply[0] = DESKTOP_REPORT_ID;
    reply[1] = op;
    reply[2] = status;
    reply[3] = token;
    reply[4] = token >> 8;
    reply[5] = op == START ? 1 : 0;  /* Feature-report batching capability. */
    reply_pending = true;
    custom_hid_report_send(&custom_hid, reply, sizeof reply);
}

bool fixfinder_desktop_run(const volatile uint32_t *clock)
{
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    if (request != START || reply_pending || doom_usb_video_busy()) {
        __set_PRIMASK(irq);
        return false;
    }
    doom_usb_video_reset();
    running = true;
    request = 0;
    __set_PRIMASK(irq);

    memset(pixels, 0, sizeof pixels);
    for (unsigned int i = 0; i < COLUMNS * ROWS; ++i) draw_tile(i);

    __disable_irq();
    if (running) acknowledge(START, 0, 1);
    __set_PRIMASK(irq);
    fixfinder_set_led(FIXFINDER_LED_GREEN);
    uint32_t last_activity = *clock;
    bool back_released = false;
    while (running) {
        uint32_t now = *clock;
        bool back = (fixfinder_physical_keys(now) & KEY_BACK) != 0;
        if (!back) back_released = true;
        if ((back && back_released) || now - last_activity >= 5000U) break;

        if (!reply_pending && batch_ready) {
            const uint8_t *data = doom_usb_video_scratch();
            uint16_t token = le16(data + 1);
            bool valid = draw_batch(data);
            __disable_irq();
            if (running) {
                batch_ready = false;
                acknowledge(BATCH, valid ? 0 : 1, token);
            }
            __set_PRIMASK(irq);
            last_activity = now;
        } else if (!reply_pending && tile_ready) {
            bool valid = !invalid && decode_tile(doom_usb_video_scratch(), expected, encoding);
            if (valid) draw_tile(tile);
            __disable_irq();
            if (running) {
                expected = 0;
                tile_ready = false;
                acknowledge(TILE, valid ? 0 : 1, sequence);
            }
            __set_PRIMASK(irq);
            last_activity = now;
        } else if (!reply_pending && request) {
            __disable_irq();
            if (running && request) {
                uint8_t op = request;
                request = 0;
                if (op == STOP) running = false;
                acknowledge(op, 0, 0);
            }
            __set_PRIMASK(irq);
            last_activity = now;
        } else {
            __WFI();
        }
    }
    __disable_irq();
    running = tile_ready = batch_ready = batch_receiving = false;
    request = 0;
    expected = 0;
    __set_PRIMASK(irq);
    return true;
}
