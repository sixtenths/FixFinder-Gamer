#ifndef FIXFINDER_USB_INPUT_H
#define FIXFINDER_USB_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#define USB_INPUT_RETURN (1U << 4)

bool doom_usb_input_request(uint8_t command);
void doom_usb_input_reset(void);
void doom_usb_input_tick(void);
uint32_t doom_usb_input_keys(void);
uint32_t doom_usb_input_buttons(void);

#endif
