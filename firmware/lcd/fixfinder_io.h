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

/* OR into hz to output noise samples at twice the listed frequency. */
enum { FIXFINDER_NOTE_NOISE = 1U << 15 };
struct fixfinder_note { uint16_t hz, ms; };

void fixfinder_io_init(void);
uint32_t fixfinder_keys(uint32_t now);
uint32_t fixfinder_physical_keys(uint32_t now);
uint32_t fixfinder_buttons(uint32_t now);
void fixfinder_health_led(int health);
void fixfinder_set_led(enum fixfinder_led led);
void fixfinder_beep(uint32_t duration_ms);
void fixfinder_tone(uint32_t hz); /* Zero stops the tone. */
/* Notes end with {0, 0}. Storage must outlive playback.
 * Equal or higher priorities can interrupt a playing cue. */
void fixfinder_play_sound(const struct fixfinder_note *notes, unsigned int priority);
void fixfinder_stop_sound(void);
void fixfinder_io_tick(void);

#endif
