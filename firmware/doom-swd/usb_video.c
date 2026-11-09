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

static unsigned int transfer_size(void)
{
    return FRAME_SIZE + (active_command != VIDEO_REQUEST);
}

void doom_usb_video_reset(void)
{
    state = IDLE;
    next_command = 0;
}

void doom_usb_video_request(uint8_t command)
{
#ifdef FIXFINDER_STORAGE_PROBE
    if (command != 0xc8U)
#endif
    if (command != VIDEO_REQUEST &&
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

void doom_usb_video_capture(const uint8_t *frame, const uint8_t *palette)
{
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    if (state != REQUESTED || palette == NULL) {
        __set_PRIMASK(irq);
        return;
    }
    const uint8_t command = requested_command;
    state = CAPTURING;
    __set_PRIMASK(irq);

    memcpy(colors, palette, sizeof colors);
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
            for (unsigned int x = 0; x < VIDEO_WIDTH; ++x) {
                row[x] = frame[y * 2 * DOOM_FRAME_WIDTH + x * 2];
            }
        }
    } else {
        const uint8_t *band = frame + (command - VIDEO_FIRST_BAND) * BAND_ROWS * DOOM_FRAME_WIDTH;
        memcpy(top, band, sizeof top);
        memcpy(bottom, band + sizeof top, sizeof bottom);
    }

    __disable_irq();
    if (state == CAPTURING) {
        offset = 0;
        active_command = command;
        state = SENDING;
        send_chunk(&custom_hid);
    }
    __set_PRIMASK(irq);
}
