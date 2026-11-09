#ifndef FIXFINDER_LOADER_SCREEN_H
#define FIXFINDER_LOADER_SCREEN_H

#include <stdbool.h>

enum loader_view {
    VIEW_CHECKING,
    VIEW_BOOT,
    VIEW_WAIT,
    VIEW_INSTALL,
    VIEW_VERIFY,
    VIEW_READY,
    VIEW_ERROR,
    VIEW_START,
#ifdef FIXFINDER_RADIO
    VIEW_WIFI
#endif
};

bool loader_screen_init(void);
void loader_screen_draw(enum loader_view view, unsigned int percent);
void loader_screen_resume_backlight(void);

#endif
