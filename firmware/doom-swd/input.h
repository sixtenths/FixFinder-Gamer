#ifndef FIXFINDER_DOOM_INPUT_H
#define FIXFINDER_DOOM_INPUT_H

/* Pressed actions from the diagnostic mailbox or a future board driver. */
enum doom_button {
    DOOM_BUTTON_FORWARD = 1U << 0,
    DOOM_BUTTON_BACK = 1U << 1,
    DOOM_BUTTON_LEFT = 1U << 2,
    DOOM_BUTTON_RIGHT = 1U << 3,
    DOOM_BUTTON_FIRE = 1U << 4,
    DOOM_BUTTON_USE = 1U << 5,
    DOOM_BUTTON_MENU = 1U << 6,
    DOOM_BUTTON_MAP = 1U << 7
};

#endif
