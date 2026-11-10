#ifndef FIXFINDER_DOOM_USB_VIDEO_H
#define FIXFINDER_DOOM_USB_VIDEO_H

#include "usbd_core.h"

#define VIDEO_REPORT_ID 0x42U
#define VIDEO_REPORT_SIZE 64U
#define VIDEO_REQUEST 0xc3U
#define VIDEO_FIRST_BAND 0xc4U
#define VIDEO_BANDS 4U
#define VIDEO_LCD_FIRST_BAND 0xc9U
#define VIDEO_LCD_BANDS 8U
#define VIDEO_PACKED_FIRST_BAND 0xd1U

void doom_usb_video_reset(void);
bool doom_usb_video_busy(void);
/* Available to the desktop mode while video capture is suspended. */
uint8_t *doom_usb_video_scratch(void);
void doom_usb_video_request(uint8_t command);
void doom_usb_video_sent(usb_dev *device);
void doom_usb_video_capture(const uint8_t *frame, const uint8_t *palette);
typedef const uint8_t *(*doom_usb_video_row_fn)(const void *context, unsigned int y);
void doom_usb_video_capture_rows(doom_usb_video_row_fn row, const void *context,
                                 const uint8_t *palette);
void doom_usb_video_capture_native_rows(doom_usb_video_row_fn native_row,
                                        doom_usb_video_row_fn preview_row,
                                        const void *context, const uint8_t *palette);
bool doom_usb_video_native_requested(void);
bool doom_usb_video_preview_requested(void);
void doom_usb_video_native_begin(const uint8_t *palette);
bool doom_usb_video_native_wants_row(unsigned int y);
void doom_usb_video_native_row(unsigned int y, const uint8_t *row);
void doom_usb_video_native_end(void);

#endif
