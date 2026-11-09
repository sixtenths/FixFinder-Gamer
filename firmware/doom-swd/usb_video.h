#ifndef FIXFINDER_DOOM_USB_VIDEO_H
#define FIXFINDER_DOOM_USB_VIDEO_H

#include "usbd_core.h"

#define VIDEO_REPORT_ID 0x42U
#define VIDEO_REPORT_SIZE 64U
#define VIDEO_REQUEST 0xc3U

void doom_usb_video_reset(void);
void doom_usb_video_request(void);
void doom_usb_video_sent(usb_dev *device);
void doom_usb_video_capture(const uint8_t *frame, const uint8_t *palette);

#endif
