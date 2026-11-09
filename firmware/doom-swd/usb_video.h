#ifndef FIXFINDER_DOOM_USB_VIDEO_H
#define FIXFINDER_DOOM_USB_VIDEO_H

#include "usbd_core.h"

#define VIDEO_REPORT_ID 0x42U
#define VIDEO_REPORT_SIZE 64U
#define VIDEO_REQUEST 0xc3U
#define VIDEO_FIRST_BAND 0xc4U
#define VIDEO_BANDS 4U

void doom_usb_video_reset(void);
void doom_usb_video_request(uint8_t command);
void doom_usb_video_sent(usb_dev *device);
void doom_usb_video_capture(const uint8_t *frame, const uint8_t *palette);
typedef const uint8_t *(*doom_usb_video_row_fn)(const void *context, unsigned int y);
void doom_usb_video_capture_rows(doom_usb_video_row_fn row, const void *context,
                                 const uint8_t *palette);

#endif
