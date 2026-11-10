#ifndef FIXFINDER_RACER_H
#define FIXFINDER_RACER_H

#include <stdint.h>
#include <stdbool.h>

enum race_result { RACE_EXIT, RACE_TIMEOUT, RACE_FINISHED };
enum race_result fixfinder_race(const volatile uint32_t *clock, bool resume);

#endif
