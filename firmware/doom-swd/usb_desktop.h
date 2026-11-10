#ifndef FIXFINDER_USB_DESKTOP_H
#define FIXFINDER_USB_DESKTOP_H

#include <stdbool.h>
#include <stdint.h>

#define DESKTOP_REPORT_ID 0x17U
#define DESKTOP_REPORT_SIZE 64U
#define DESKTOP_BATCH_ID 0x18U
#define DESKTOP_BATCH_SIZE 4096U

void desktop_usb_receive(const uint8_t *report);
uint8_t *desktop_usb_batch_buffer(void);
void desktop_usb_batch_received(void);
void desktop_usb_reset(void);
bool desktop_usb_sent(void);
bool desktop_usb_active(void);
bool fixfinder_desktop_run(const volatile uint32_t *clock);

#endif
