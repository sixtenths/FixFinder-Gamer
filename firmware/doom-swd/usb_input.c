#include "usb_input.h"
#include "gd32f4xx.h"

/* Three five-bit fragments, committed by the final report. The upper half
 * of the published word is a lease in milliseconds for the lower half. */
#define INPUT_LEASE (250U << 16)
static uint32_t pending;
static uint8_t fragments;
static volatile uint32_t active;

void doom_usb_input_reset(void)
{
    pending = 0;
    fragments = 0;
    active = 0;
}

bool doom_usb_input_request(uint8_t command)
{
    uint32_t bits = command & 31U;
    switch (command >> 5) {
    case 0:
        if (command != 0x1fU) return false;
        doom_usb_input_reset();
        return true;
    case 1:
        pending = bits;
        fragments = 1;
        return true;
    case 2:
        if (fragments == 1) {
            pending |= bits << 5;
            fragments = 2;
        }
        return true;
    case 3:
        if (fragments == 2) active = INPUT_LEASE | pending | (bits << 10);
        fragments = 0;
        return true;
    default:
        return false;
    }
}

void doom_usb_input_tick(void)
{
    if (!active) return;
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    if (active >= (2U << 16)) active -= 1U << 16;
    else active = 0;
    __set_PRIMASK(irq);
}

uint32_t doom_usb_input_keys(void)
{
    return active & 31U;
}

uint32_t doom_usb_input_buttons(void)
{
    return (active >> 5) & 1023U;
}
