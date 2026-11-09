#ifndef FIXFINDER_IO_H
#define FIXFINDER_IO_H

#include <stdint.h>

void fixfinder_io_init(void);
uint32_t fixfinder_buttons(uint32_t now);
void fixfinder_health_led(int health);
void fixfinder_beep(uint32_t duration_ms);
void fixfinder_io_tick(void);

#endif
