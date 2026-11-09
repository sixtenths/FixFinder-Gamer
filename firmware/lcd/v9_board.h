#ifndef FIXFINDER_V9_BOARD_H
#define FIXFINDER_V9_BOARD_H

#include "hx8347.h"

struct v9_lcd_board {
    struct hx8347_bus bus;
    uint8_t expected_id;
    bool landscape;
};

/* Implement with measured board wiring and a confirmed controller sequence.
 * Return NULL on failure. Keep the returned transport valid for the test. */
const struct v9_lcd_board *v9_lcd_board_init(void);

#endif
