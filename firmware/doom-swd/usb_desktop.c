#include "usb_desktop.h"
#include "usb_video.h"
#include "display.h"
#include "../lcd/fixfinder_io.h"
#include "custom_hid_core.h"
#include "jpeg/tjpgd.h"
#include <string.h>

enum { START = 1, TILE, DATA, STOP, PING, BATCH };
enum { RAW, RUNS, INDEXED, MONO, FOUR_COLORS, LZNT1, PATCH };
enum { TILE_W = 40, TILE_H = 20, PIXELS = TILE_W * TILE_H,
       COLUMNS = DOOM_LCD_WIDTH / TILE_W, ROWS = DOOM_LCD_HEIGHT / TILE_H };
enum { JPEG_BATCH = 0x8000, JPEG_CAPACITY = 24 * 1024, JPEG_WORK = 10 * 1024 };
_Static_assert(JPEG_CAPACITY + JPEG_WORK <= DOOM_FRAME_WIDTH * DOOM_FRAME_HEIGHT,
               "Desktop JPEG must fit the paused arcade framebuffer");

struct jpeg_frame {
    uint8_t *data;
    unsigned int size, written, cursor;
};

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
    } else if (op == BATCH && !tile_ready && !expected) {
        unsigned int size = le16(packet + 4);
        if (size <= 5 || size >= DESKTOP_REPORT_SIZE) return;
        uint8_t *data = doom_usb_video_scratch();
        data[0] = DESKTOP_BATCH_ID;
        memcpy(data + 1, packet + 2, size - 1);
        batch_ready = true;
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

/* A tile fits in one LZNT1 chunk. Decode directly into the RGB565 buffer. */
static bool decode_lznt1(const uint8_t *data, unsigned int size, unsigned int capacity)
{
    if (size < 3) return false;
    unsigned int header = le16(data);
    if ((header & 0xf000U) != 0xb000U || (header & 0xfffU) + 3U != size) return false;
    uint8_t *output = (uint8_t *)pixels;
    unsigned int out = 0;
    for (unsigned int i = 2; i < size;) {
        unsigned int flags = data[i++];
        if (i == size) return false;
        for (unsigned int bit = 0; bit < 8 && i < size; ++bit) {
            if (!(flags & (1U << bit))) {
                if (out == capacity) return false;
                output[out++] = data[i++];
                continue;
            }
            if (!out || size - i < 2) return false;
            unsigned int shift = 12;
            for (unsigned int position = out - 1; position >= 16; position >>= 1) --shift;
            unsigned int match = le16(data + i);
            unsigned int distance = (match >> shift) + 1;
            unsigned int length = (match & ((1U << shift) - 1U)) + 3;
            i += 2;
            if (distance > out || length > capacity - out) return false;
            while (length--) {
                output[out] = output[out - distance];
                ++out;
            }
        }
    }
    return out == capacity;
}

static bool decode_tile(const uint8_t *data, unsigned int size, unsigned int format,
                        unsigned int count_pixels)
{
    if (!size) return false;
    if (format == LZNT1) {
        return decode_lznt1(data, size, 2 * count_pixels);
    } else if (format == RAW) {
        if (size != 2 * count_pixels) return false;
        memcpy(pixels, data, size);
    } else if (format == RUNS) {
        if (size % 3) return false;
        unsigned int out = 0;
        for (unsigned int i = 0; i < size; i += 3) {
            unsigned int length = data[i];
            uint16_t color = le16(data + i + 1);
            if (!length || length > count_pixels - out) return false;
            while (length--) pixels[out++] = color;
        }
        if (out != count_pixels) return false;
    } else if (format <= FOUR_COLORS) {
        unsigned int bits = format == INDEXED ? 4 : format == MONO ? 1 : 2;
        unsigned int mask = (1U << bits) - 1U;
        unsigned int count = data[0];
        if (!count || count > mask + 1U || size != 1 + 2 * count + (count_pixels * bits + 7) / 8)
            return false;
        const uint8_t *indices = data + 1 + 2 * count;
        for (unsigned int i = 0; i < count_pixels; ++i) {
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

static size_t jpeg_read(JDEC *decoder, uint8_t *dest, size_t requested)
{
    struct jpeg_frame *frame = decoder->device;
    size_t left = frame->size - frame->cursor;
    size_t count = requested < left ? requested : left;
    if (dest) memcpy(dest, frame->data + frame->cursor, count);
    frame->cursor += count;
    return count;
}

static int jpeg_draw(JDEC *decoder, void *pixels_rgb565, JRECT *rect)
{
    unsigned int width = rect->right - rect->left + 1;
    unsigned int height = rect->bottom - rect->top + 1;
    if (decoder->width == DOOM_LCD_WIDTH) {
        doom_dcs_write_rgb_rect(rect->left, rect->top, width, height, pixels_rgb565);
        return 1;
    }

    /* The assembled JPEG and its decoder are in the arcade frame buffer. The
       USB batch scratch is free until we acknowledge this frame. */
    const uint16_t *source = pixels_rgb565;
    uint16_t *scaled = (uint16_t *)doom_usb_video_scratch();
    unsigned int stride = 2 * width;
    for (unsigned int y = 0; y < height; ++y) {
        uint16_t *row = scaled + 2 * y * stride;
        for (unsigned int x = 0; x < width; ++x) {
            uint16_t pixel = *source++;
            row[2 * x] = row[2 * x + 1] = pixel;
        }
        memcpy(row + stride, row, stride * sizeof *row);
    }
    doom_dcs_write_rgb_rect(2 * rect->left, 2 * rect->top, stride, 2 * height, scaled);
    return 1;
}

static bool draw_jpeg_chunk(struct jpeg_frame *frame, const uint8_t *data, unsigned int end)
{
    if (end <= 9 || end > DESKTOP_BATCH_SIZE) return false;
    unsigned int offset = le16(data + 5), size = le16(data + 7), count = end - 9;
    if (!offset) {
        frame->size = size;
        frame->written = frame->cursor = 0;
    }
    if (!size || size > JPEG_CAPACITY || size != frame->size ||
        offset != frame->written || offset >= size || count > size - offset)
        return false;
    memcpy(frame->data + offset, data + 9, count);
    frame->written += count;
    if (frame->written < size) return true;

    JDEC decoder;
    if (jd_prepare(&decoder, jpeg_read, frame->data + JPEG_CAPACITY, JPEG_WORK, frame) != JDR_OK)
        return false;
    bool native = decoder.width == DOOM_LCD_WIDTH && decoder.height == DOOM_LCD_HEIGHT;
    bool half = decoder.width == DOOM_LCD_WIDTH / 2 && decoder.height == DOOM_LCD_HEIGHT / 2;
    return (native || half) && jd_decomp(&decoder, jpeg_draw, 0) == JDR_OK;
}

static bool draw_batch(const uint8_t *data, struct jpeg_frame *jpeg)
{
    /* Report ID, sequence and used length, followed by tile records and padding. */
    unsigned int end = le16(data + 3);
    if (data[0] != DESKTOP_BATCH_ID) return false;
    if (end & JPEG_BATCH) return draw_jpeg_chunk(jpeg, data, end & ~JPEG_BATCH);
    if (end <= 5 || end > DESKTOP_BATCH_SIZE)
        return false;
    for (unsigned int offset = 5; offset < end;) {
        if (end - offset < 4) return false;
        unsigned int index = data[offset], format = data[offset + 1];
        unsigned int size = le16(data + offset + 2);
        offset += 4;
        if (index >= COLUMNS * ROWS || size > end - offset) return false;
        if (format == PATCH) {
            if (size < 5) return false;
            unsigned int x = data[offset], y = data[offset + 1];
            unsigned int width = data[offset + 2], height = data[offset + 3];
            if (!width || !height || x + width > TILE_W || y + height > TILE_H ||
                !decode_tile(data + offset + 5, size - 5, data[offset + 4], width * height))
                return false;
            doom_dcs_write_rgb_rect(index % COLUMNS * TILE_W + x, index / COLUMNS * TILE_H + y,
                                    width, height, pixels);
        } else {
            if (!decode_tile(data + offset, size, format, PIXELS)) return false;
            draw_tile(index);
        }
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
    reply[6] = op == START ? 1 : 0;  /* Compressed rectangles and short batches. */
    reply[7] = op == START ? 1 : 0;  /* Full-frame JPEG video mode. */
    reply_pending = true;
    custom_hid_report_send(&custom_hid, reply, sizeof reply);
}

bool fixfinder_desktop_run(const volatile uint32_t *clock, uint8_t *frame)
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
    struct jpeg_frame jpeg = {.data = frame};
    while (running) {
        uint32_t now = *clock;
        bool back = (fixfinder_physical_keys(now) & KEY_BACK) != 0;
        if (!back) back_released = true;
        if ((back && back_released) || now - last_activity >= 5000U) break;

        if (!reply_pending && batch_ready) {
            const uint8_t *data = doom_usb_video_scratch();
            uint16_t token = le16(data + 1);
            bool valid = draw_batch(data, &jpeg);
            __disable_irq();
            if (running) {
                batch_ready = false;
                acknowledge(BATCH, valid ? 0 : 1, token);
            }
            __set_PRIMASK(irq);
            last_activity = now;
        } else if (!reply_pending && tile_ready) {
            bool valid = !invalid && decode_tile(doom_usb_video_scratch(), expected, encoding, PIXELS);
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
