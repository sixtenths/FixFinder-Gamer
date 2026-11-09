#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "gd32f4xx.h"
#include "doomdef.h"
#include "d_event.h"
#include "d_main.h"
#include "global_data.h"
#include "i_sound.h"
#include "i_system_e32.h"
#include "i_video.h"
#include "z_zone.h"
#include "display.h"
#include "input.h"

_Static_assert(SCREENWIDTH * sizeof(uint16_t) == DOOM_FRAME_WIDTH, "Doom pitch changed");
_Static_assert(SCREENHEIGHT == DOOM_FRAME_HEIGHT, "Doom height changed");

/* SWD can read these symbols using the linked ELF. */
volatile unsigned int doom_frames;
volatile unsigned int doom_frame_hash;
volatile unsigned int doom_level_frames;
volatile uint32_t doom_test_buttons;
volatile int32_t doom_player_x;
volatile int32_t doom_player_y;
volatile int32_t doom_player_bullets;
volatile uint32_t doom_player_weapon;
char doom_error[160];
static uint16_t doom_frame[SCREENWIDTH * SCREENHEIGHT]
    __attribute__((section(".doom_frame")));
static volatile uint32_t milliseconds;
static uint32_t frame_hash;

extern char _ebss;
extern char __heap_end;

void SysTick_Handler(void)
{
    milliseconds++;
}

clock_t clock(void)
{
    return (clock_t)((uint64_t)milliseconds * CLOCKS_PER_SEC / 1000U);
}

void *_sbrk(ptrdiff_t increment)
{
    static uintptr_t current;
    const uintptr_t start = (uintptr_t)&_ebss;
    const uintptr_t limit = (uintptr_t)&__heap_end;
    if (current == 0) {
        current = start;
    }
    if (increment > (ptrdiff_t)(limit - current) ||
        increment < -(ptrdiff_t)(current - start)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    const uintptr_t previous = current;
    current += increment;
    return (void *)previous;
}

int _write(int fd, const char *data, int count)
{
    (void)fd;
    (void)data;
    return count;
}

void I_Error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(doom_error, sizeof(doom_error), format, args);
    va_end(args);
    for (;;) {
        __WFI();
    }
}

void I_InitScreen_e32(void) {}
void I_ProcessKeyEvents(void)
{
    static const struct {
        uint32_t button;
        int key;
    } bindings[] = {
        { DOOM_BUTTON_FORWARD, KEYD_UP },
        { DOOM_BUTTON_BACK, KEYD_DOWN },
        { DOOM_BUTTON_LEFT, KEYD_LEFT },
        { DOOM_BUTTON_RIGHT, KEYD_RIGHT },
        { DOOM_BUTTON_FIRE, KEYD_B },
        { DOOM_BUTTON_USE, KEYD_A },
        { DOOM_BUTTON_MENU, KEYD_START },
        { DOOM_BUTTON_MAP, KEYD_SELECT },
        { DOOM_BUTTON_STRAFE_LEFT, KEYD_L },
        { DOOM_BUTTON_STRAFE_RIGHT, KEYD_R },
    };
    static uint32_t previous;
    const uint32_t pressed = doom_test_buttons;
    const uint32_t changed = pressed ^ previous;
    previous = pressed;
    for (unsigned int i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
        if ((changed & bindings[i].button) != 0U) {
            event_t event = {
                .type = (pressed & bindings[i].button) != 0U ? ev_keydown : ev_keyup,
                .data1 = bindings[i].key
            };
            D_PostEvent(&event);
        }
    }
}
void I_SetPallete_e32(const byte *palette) { doom_display_set_palette(palette); }
void I_CreateBackBuffer_e32(void) { memset(doom_frame, 0, sizeof(doom_frame)); }
unsigned short *I_GetBackBuffer(void) { return doom_frame; }
unsigned short *I_GetFrontBuffer(void) { return doom_frame; }

void doom_lcd_write_row(unsigned int row, const uint16_t *pixels)
{
    /* The diagnostic consumes the same RGB565 rows as a physical LCD backend. */
    for (unsigned int x = row % 31U; x < DOOM_LCD_WIDTH; x += 31U) {
        frame_hash = (frame_hash ^ pixels[x]) * 16777619U;
    }
}

void I_FinishUpdate_e32(const byte *frame, const byte *palette,
                        const unsigned int width, const unsigned int height)
{
    (void)palette;
    (void)width;
    (void)height;
    frame_hash = 2166136261U;
    doom_display_present(frame);
    doom_frame_hash = frame_hash;
    if (_g->gamestate == GS_LEVEL && _g->player.mo != NULL) {
        doom_player_x = _g->player.mo->x;
        doom_player_y = _g->player.mo->y;
        doom_player_bullets = _g->player.ammo[am_clip];
        doom_player_weapon = _g->player.readyweapon;
        doom_level_frames++;
    }
    doom_frames++;
}

int I_StartSound(int id, int channel, int volume, int separation)
{
    (void)id;
    (void)volume;
    (void)separation;
    return channel;
}

void I_SetMusicVolume(int volume) { (void)volume; }
void I_PauseSong(int handle) { (void)handle; }
void I_ResumeSong(int handle) { (void)handle; }
void I_PlaySong(int handle, int looping) { (void)handle; (void)looping; }
void I_StopSong(int handle) { (void)handle; }

void fixfinder_doom_run(void)
{
    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0) {
        I_Error("SysTick setup failed");
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    I_PreInitGraphics();
    Z_Init();
    InitGlobals();
    D_DoomMain();
}
