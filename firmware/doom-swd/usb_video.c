#include "usb_video.h"
#include "custom_hid_core.h"
#include "display.h"
#include <string.h>

#define VIDEO_WIDTH (DOOM_FRAME_WIDTH / 2)
#define VIDEO_HEIGHT (DOOM_FRAME_HEIGHT / 2)
#define HALF_SIZE (VIDEO_WIDTH * VIDEO_HEIGHT / 2)
#define PALETTE_SIZE (256U * 3U)
#define FRAME_SIZE (PALETTE_SIZE + HALF_SIZE * 2U)
#define PAYLOAD_SIZE (VIDEO_REPORT_SIZE - 3U)
#define BAND_ROWS (DOOM_FRAME_HEIGHT / VIDEO_BANDS)
#define LCD_BAND_ROWS (DOOM_LCD_HEIGHT / VIDEO_LCD_BANDS)

_Static_assert(DOOM_LCD_WIDTH * LCD_BAND_ROWS == HALF_SIZE * 2U && !(LCD_BAND_ROWS % 2),
               "LCD band must fill both snapshot buffers");

extern usb_core_driver custom_hid;

/* Split the snapshot across SRAM and TCM to leave room for the game heap and
 * the optional map setup buffer. USB sends only from the SRAM report buffer. */
static uint8_t top[HALF_SIZE];
static uint8_t bottom[HALF_SIZE] __attribute__((section(".doom_video")));
static uint8_t colors[PALETTE_SIZE];
static uint8_t report[VIDEO_REPORT_SIZE] __attribute__((aligned(4)));
static volatile enum { IDLE, REQUESTED, CAPTURING, SENDING } state;
static volatile uint8_t requested_command;
static uint8_t active_command;
static uint8_t next_command;
static uint16_t offset;
static volatile bool native_capture;

static bool is_lcd_band(uint8_t command)
{
    return command >= VIDEO_LCD_FIRST_BAND && command < VIDEO_LCD_FIRST_BAND + VIDEO_LCD_BANDS;
}

static unsigned int transfer_size(void)
{
    return FRAME_SIZE + (active_command != VIDEO_REQUEST);
}

void doom_usb_video_reset(void)
{
    state = IDLE;
    next_command = 0;
    native_capture = false;
}

void doom_usb_video_request(uint8_t command)
{
#ifdef FIXFINDER_STORAGE_PROBE
    if (command != 0xc8U)
#endif
    if (command != VIDEO_REQUEST && !is_lcd_band(command) &&
        (command < VIDEO_FIRST_BAND || command >= VIDEO_FIRST_BAND + VIDEO_BANDS)) {
        return;
    }
    if (state == IDLE) {
        requested_command = command;
        state = REQUESTED;
    } else if (state == SENDING && offset >= transfer_size()) {
        /* The host may request the next frame before the final IN callback. */
        next_command = command;
    }
}

static void send_chunk(usb_dev *device)
{
    report[0] = VIDEO_REPORT_ID;
    report[1] = (uint8_t)offset;
    report[2] = (uint8_t)(offset >> 8);
    for (unsigned int i = 0; i < PAYLOAD_SIZE; ++i) {
        unsigned int index = offset + i;
        if (active_command != VIDEO_REQUEST) {
            if (index == 0) {
                report[i + 3] = active_command;
                continue;
            }
            --index;
        }
        if (index < PALETTE_SIZE) {
            report[i + 3] = colors[index];
        } else if (index < PALETTE_SIZE + HALF_SIZE) {
            report[i + 3] = top[index - PALETTE_SIZE];
        } else if (index < FRAME_SIZE) {
            report[i + 3] = bottom[index - PALETTE_SIZE - HALF_SIZE];
        } else {
            report[i + 3] = 0;
        }
    }
    offset += PAYLOAD_SIZE;
    custom_hid_report_send(device, report, sizeof report);
}

void doom_usb_video_sent(usb_dev *device)
{
    if (state == SENDING) {
        if (offset < transfer_size()) {
            send_chunk(device);
        } else {
            requested_command = next_command;
            state = next_command ? REQUESTED : IDLE;
            next_command = 0;
        }
    }
}

static uint8_t begin_capture(const uint8_t *palette, bool native_only)
{
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    if (state != REQUESTED || palette == NULL ||
        (native_only && !is_lcd_band(requested_command))) {
        __set_PRIMASK(irq);
        return 0;
    }
    const uint8_t command = requested_command;
    state = CAPTURING;
    __set_PRIMASK(irq);

    memcpy(colors, palette, sizeof colors);
    return command;
}

static void end_capture(uint8_t command)
{
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    if (state == CAPTURING) {
        offset = 0;
        active_command = command;
        state = SENDING;
        send_chunk(&custom_hid);
    }
    __set_PRIMASK(irq);
}

static uint8_t *lcd_band_row(unsigned int y)
{
    return (y < LCD_BAND_ROWS / 2 ? top : bottom) +
           (y % (LCD_BAND_ROWS / 2)) * DOOM_LCD_WIDTH;
}

void doom_usb_video_capture_native_rows(doom_usb_video_row_fn native_row,
                                        doom_usb_video_row_fn get_row,
                                        const void *context, const uint8_t *palette)
{
    const uint8_t command = begin_capture(palette, false);
    if (!command) return;

#ifdef FIXFINDER_STORAGE_PROBE
    if (command == 0xc8U) {
        extern void fixfinder_emmc_snapshot(uint8_t *dest);
        memset(top,0,sizeof top);
        memset(bottom,0,sizeof bottom);
        fixfinder_emmc_snapshot(top);
    } else
#endif
    if (command == VIDEO_REQUEST) {
        for (unsigned int y = 0; y < VIDEO_HEIGHT; ++y) {
            uint8_t *row = (y < VIDEO_HEIGHT / 2 ? top : bottom) +
                           (y % (VIDEO_HEIGHT / 2)) * VIDEO_WIDTH;
            const uint8_t *source = get_row(context, y * 2);
            for (unsigned int x = 0; x < VIDEO_WIDTH; ++x) {
                row[x] = source[x * 2];
            }
        }
    } else if (is_lcd_band(command)) {
        const unsigned int first = (command - VIDEO_LCD_FIRST_BAND) * LCD_BAND_ROWS;
        for (unsigned int y = 0; y < LCD_BAND_ROWS; ++y) {
            uint8_t *row = lcd_band_row(y);
            if (native_row) {
                memcpy(row, native_row(context, first + y), DOOM_LCD_WIDTH);
            } else {
                const uint8_t *source = get_row(context, (first + y) * DOOM_FRAME_HEIGHT / DOOM_LCD_HEIGHT);
                for (unsigned int x = 0; x < DOOM_LCD_WIDTH; ++x)
                    row[x] = source[x * DOOM_FRAME_WIDTH / DOOM_LCD_WIDTH];
            }
        }
    } else {
        const unsigned int first = (command - VIDEO_FIRST_BAND) * BAND_ROWS;
        for (unsigned int y = 0; y < BAND_ROWS; ++y) {
            uint8_t *row = (y < BAND_ROWS / 2 ? top : bottom) +
                           (y % (BAND_ROWS / 2)) * DOOM_FRAME_WIDTH;
            memcpy(row, get_row(context, first + y), DOOM_FRAME_WIDTH);
        }
    }

    end_capture(command);
}

bool doom_usb_video_native_requested(void)
{
    return state == REQUESTED && is_lcd_band(requested_command);
}

void doom_usb_video_native_begin(const uint8_t *palette)
{
    native_capture = begin_capture(palette, true) != 0;
}

void doom_usb_video_native_row(unsigned int y, const uint8_t *row)
{
    if (!native_capture) return;
    unsigned int first = (requested_command - VIDEO_LCD_FIRST_BAND) * LCD_BAND_ROWS;
    if (y >= first && y < first + LCD_BAND_ROWS)
        memcpy(lcd_band_row(y - first), row, DOOM_LCD_WIDTH);
}

void doom_usb_video_native_end(void)
{
    if (!native_capture) return;
    native_capture = false;
    end_capture(requested_command);
}

void doom_usb_video_capture_rows(doom_usb_video_row_fn get_row, const void *context,
                                 const uint8_t *palette)
{
    doom_usb_video_capture_native_rows(NULL, get_row, context, palette);
}

static const uint8_t *frame_row(const void *frame, unsigned int y)
{
    return (const uint8_t *)frame + y * DOOM_FRAME_WIDTH;
}

void doom_usb_video_capture(const uint8_t *frame, const uint8_t *palette)
{
    doom_usb_video_capture_rows(frame_row, frame, palette);
}
