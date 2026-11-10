#include "usb_desktop.h"
#include "usb_video.h"
#include "display.h"
#include "../lcd/fixfinder_io.h"
#include "custom_hid_core.h"
#include <string.h>

enum { START = 1, TILE, DATA, STOP, PING };
enum { RAW, RUNS, INDEXED };
enum { TILE_W = 40, TILE_H = 20, PIXELS = TILE_W * TILE_H,
       COLUMNS = DOOM_LCD_WIDTH / TILE_W, ROWS = DOOM_LCD_HEIGHT / TILE_H };

extern usb_core_driver custom_hid;
static uint16_t pixels[PIXELS] __attribute__((section(".doom_video")));
static uint8_t reply[DESKTOP_REPORT_SIZE] __attribute__((aligned(4)));
static volatile bool running, reply_pending, tile_ready;
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
    running = reply_pending = tile_ready = false;
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

static bool decode_tile(void)
{
    const uint8_t *data = doom_usb_video_scratch();
    if (invalid) return false;
    if (encoding == RAW) {
        if (expected != sizeof pixels) return false;
        memcpy(pixels, data, sizeof pixels);
    } else if (encoding == RUNS) {
        if (expected % 3) return false;
        unsigned int out = 0;
        for (unsigned int i = 0; i < expected; i += 3) {
            unsigned int length = data[i];
            uint16_t color = le16(data + i + 1);
            if (!length || length > PIXELS - out) return false;
            while (length--) pixels[out++] = color;
        }
        if (out != PIXELS) return false;
    } else {
        unsigned int count = data[0];
        if (!count || count > 16 || expected != 1 + 2 * count + PIXELS / 2) return false;
        const uint8_t *indices = data + 1 + 2 * count;
        for (unsigned int i = 0; i < PIXELS; ++i) {
            unsigned int color = (indices[i / 2] >> ((i % 2) * 4)) & 15;
            if (color >= count) return false;
            pixels[i] = le16(data + 1 + 2 * color);
        }
    }
    return true;
}

static void draw_tile(unsigned int index)
{
    doom_dcs_write_rgb_rect(index % COLUMNS * TILE_W, index / COLUMNS * TILE_H,
                            TILE_W, TILE_H, pixels);
}

/* Call with interrupts masked after checking that the IN pipe is idle. */
static void acknowledge(uint8_t op, uint8_t status, uint16_t token)
{
    reply[0] = DESKTOP_REPORT_ID;
    reply[1] = op;
    reply[2] = status;
    reply[3] = token;
    reply[4] = token >> 8;
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

        if (!reply_pending && tile_ready) {
            bool valid = decode_tile();
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
    running = tile_ready = false;
    request = 0;
    expected = 0;
    __set_PRIMASK(irq);
    return true;
}
