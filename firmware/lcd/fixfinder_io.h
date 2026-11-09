#ifndef FIXFINDER_IO_H
#define FIXFINDER_IO_H

#include <stdint.h>

enum fixfinder_key {
    KEY_BACK = 1U << 0,
    KEY_DOWN = 1U << 1,
    KEY_ENTER = 1U << 2,
    KEY_ERASE = 1U << 3
};

enum fixfinder_led { FIXFINDER_LED_OFF, FIXFINDER_LED_GREEN, FIXFINDER_LED_YELLOW, FIXFINDER_LED_RED };

void fixfinder_io_init(void);
uint32_t fixfinder_keys(uint32_t now);
uint32_t fixfinder_physical_keys(uint32_t now);
uint32_t fixfinder_buttons(uint32_t now);
void fixfinder_health_led(int health);
void fixfinder_set_led(enum fixfinder_led led);
void fixfinder_beep(uint32_t duration_ms);
void fixfinder_tone(uint32_t hz); /* Zero stops the tone. */
#define FIXFINDER_PCM_RATE 48000U
void fixfinder_pcm_start(int16_t (*sample)(void)); /* Samples must be -512 through 511. */
void fixfinder_pcm_stop(void);
void fixfinder_io_tick(void);

#endif
