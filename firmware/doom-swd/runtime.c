#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "gd32f4xx.h"
#ifdef FIXFINDER_USB_VIDEO
#include "usb_input.h"
#include "usb_video.h"
#endif
#include "doomdef.h"
#include "d_event.h"
#include "d_main.h"
#include "global_data.h"
#include "i_sound.h"
#include "i_system_e32.h"
#include "i_video.h"
#include "z_zone.h"
#include "display.h"
#ifdef FIXFINDER_SAVE_FILES
#include "save_files.h"
#include "doom_saves.h"

#ifdef FIXFINDER_EXTERNAL_DOOM_MAPS
#define DOOM_SAVE_NAME "DOOMEP1"
#else
#define DOOM_SAVE_NAME "DOOMMAP1"
#endif

bool fixfinder_doom_save(const uint8_t *data, unsigned int size, unsigned int offset)
{
    return fixfinder_save_write(offset ? "DOOMCFG" : DOOM_SAVE_NAME, data, size);
}

void fixfinder_doom_load(uint8_t *data, unsigned int size, unsigned int offset)
{
    if (!fixfinder_save_read(offset ? "DOOMCFG" : DOOM_SAVE_NAME, data, size))
        memset(data, 0, size);
}
#endif
#include "input.h"
#ifdef FIXFINDER_ARCADE
#include "arcade.h"
#include "../usb-loader/protocol.h"
#endif
#ifdef FIXFINDER_DOOM_BOARD
#include "v9_board.h"
static const struct v9_lcd_board *doom_board;
#endif
#ifdef FIXFINDER_DOOM_DCS
#include "../lcd/fixfinder_io.h"
#include "sounds.h"
bool doom_dcs_init(void);
void doom_dcs_present(const uint8_t *frame);

enum sound_priority { AMBIENCE = 1, IMPACT, WEAPON, EXPLOSION, ALERT };

/* Noise at the attack gives the shot a crack. The lower notes carry its
 * body, and the gaps separate the reflections. */
#define CRACK(hz, ms) { FIXFINDER_NOTE_NOISE | (hz), (ms) }
static const struct fixfinder_note pistol[] = {
    CRACK(2600, 7), {870, 4}, {325, 14}, {470, 6}, {231, 15}, {0, 7}, {178, 12}, {0, 0}
};
static const struct fixfinder_note chaingun[] = {
    CRACK(2200, 6), {690, 4}, {298, 14}, {214, 12}, {0, 0}
};
static const struct fixfinder_note shotgun[] = {
    CRACK(3000, 9), {570, 4}, CRACK(1450, 5), {288, 17}, {403, 8},
    {221, 23}, {0, 9}, {178, 18}, {0, 0}
};
static const struct fixfinder_note double_barrel[] = {
    CRACK(3400, 9), {340, 9}, CRACK(2500, 10), {610, 5},
    {258, 23}, {372, 10}, {194, 25}, {0, 9}, {155, 24}, {0, 0}
};
static const struct fixfinder_note rocket[] = {
    CRACK(1700, 8), {660, 6}, {278, 14}, {360, 10}, {491, 13}, {630, 18}, {0, 0}
};
static const struct fixfinder_note plasma[] = {
    {2080, 4}, CRACK(1800, 7), {1710, 6}, {705, 9}, {425, 12}, {0, 0}
};
static const struct fixfinder_note bfg[] = {
    {270, 25}, {430, 20}, {710, 16}, {1130, 12}, CRACK(2800, 14),
    {1410, 5}, {292, 22}, {186, 35}, {0, 0}
};
static const struct fixfinder_note blast[] = {
    CRACK(1900, 12), {270, 21}, {356, 9}, {203, 24},
    {0, 10}, {166, 32}, {0, 13}, {136, 22}, {0, 0}
};
#undef CRACK
static const struct fixfinder_note punch[] = {{590, 5}, {275, 19}, {170, 13}, {0, 0}};
static const struct fixfinder_note saw[] = {{415, 11}, {550, 14}, {320, 23}, {0, 0}};
static const struct fixfinder_note cock[] = {{580, 12}, {0, 15}, {860, 11}, {340, 17}, {0, 0}};
static const struct fixfinder_note pickup[] = {{880, 27}, {1320, 36}, {0, 0}};
static const struct fixfinder_note weapon[] = {{660, 25}, {990, 25}, {1480, 50}, {0, 0}};
static const struct fixfinder_note door_open[] = {{170, 27}, {220, 33}, {285, 40}, {0, 0}};
static const struct fixfinder_note door_close[] = {{285, 32}, {220, 28}, {165, 36}, {0, 0}};
static const struct fixfinder_note click[] = {{1050, 10}, {430, 22}, {0, 0}};
static const struct fixfinder_note pain[] = {{510, 23}, {320, 22}, {220, 34}, {0, 0}};
static const struct fixfinder_note death[] = {{545, 34}, {385, 44}, {240, 58}, {140, 75}, {0, 0}};
#endif

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
#ifdef FIXFINDER_USB_VIDEO
    doom_usb_input_tick();
#endif
#ifdef FIXFINDER_DOOM_DCS
    fixfinder_io_tick();
#endif
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
    uint32_t pressed = doom_test_buttons;
#ifdef FIXFINDER_USB_VIDEO
    pressed |= doom_usb_input_buttons();
#endif
#ifdef FIXFINDER_DOOM_BOARD
    pressed |= doom_board->read_buttons();
#elif defined(FIXFINDER_DOOM_DCS)
#ifdef FIXFINDER_ARCADE
    uint32_t exit_keys = fixfinder_physical_keys(milliseconds);
#ifdef FIXFINDER_USB_VIDEO
    if (doom_usb_input_keys() & USB_INPUT_RETURN) exit_keys = KEY_DOWN | KEY_BACK;
#endif
    if (fixfinder_arcade_exit(exit_keys, milliseconds)) {
        BOOT_COOKIE[0] = BOOT_GAME;
        BOOT_COOKIE[1] = ~BOOT_GAME;
        NVIC_SystemReset();
    }
#endif
    pressed |= fixfinder_buttons(milliseconds);
#endif
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
    /* Sample the same RGB565 rows in both builds for the SWD frame hash. */
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
#ifdef FIXFINDER_DOOM_BOARD
    if (!doom_display_hx8347(frame, &doom_board->bus, doom_lcd_write_row)) {
        I_Error("LCD frame transfer failed");
    }
#elif defined(FIXFINDER_DOOM_DCS)
    doom_dcs_present(frame);
#else
    doom_display_present(frame);
#endif
#ifdef FIXFINDER_USB_VIDEO
    doom_usb_video_capture(frame, palette);
#endif
    doom_frame_hash = frame_hash;
    if (_g->gamestate == GS_LEVEL && _g->player.mo != NULL) {
        doom_player_x = _g->player.mo->x;
        doom_player_y = _g->player.mo->y;
        doom_player_bullets = _g->player.ammo[am_clip];
        doom_player_weapon = _g->player.readyweapon;
#ifdef FIXFINDER_DOOM_DCS
        fixfinder_health_led(_g->player.health);
#endif
        doom_level_frames++;
    }
    doom_frames++;
}

int I_StartSound(int id, int channel, int volume, int separation)
{
#ifdef FIXFINDER_DOOM_DCS
    if (volume > 0) {
        switch (id) {
        case sfx_pistol:
            fixfinder_play_sound(pistol, WEAPON);
            break;
        case sfx_chgun:
            fixfinder_play_sound(chaingun, WEAPON);
            break;
        case sfx_shotgn:
            fixfinder_play_sound(shotgun, WEAPON);
            break;
        case sfx_dshtgn:
            fixfinder_play_sound(double_barrel, WEAPON);
            break;
        case sfx_punch:
            fixfinder_play_sound(punch, WEAPON);
            break;
        case sfx_sawup:
        case sfx_sawful:
        case sfx_sawhit:
            fixfinder_play_sound(saw, WEAPON);
            break;
        case sfx_sgcock:
        case sfx_dbopn:
        case sfx_dbcls:
        case sfx_dbload:
            fixfinder_play_sound(cock, AMBIENCE);
            break;
        case sfx_rlaunc:
        case sfx_firsht:
            fixfinder_play_sound(rocket, WEAPON);
            break;
        case sfx_plasma:
            fixfinder_play_sound(plasma, WEAPON);
            break;
        case sfx_bfg:
            fixfinder_play_sound(bfg, WEAPON);
            break;
        case sfx_rxplod:
        case sfx_firxpl:
        case sfx_barexp:
            fixfinder_play_sound(blast, EXPLOSION);
            break;
        case sfx_itemup:
            fixfinder_play_sound(pickup, AMBIENCE);
            break;
        case sfx_wpnup:
        case sfx_getpow:
            fixfinder_play_sound(weapon, IMPACT);
            break;
        case sfx_doropn:
        case sfx_bdopn:
        case sfx_pstart:
            fixfinder_play_sound(door_open, AMBIENCE);
            break;
        case sfx_dorcls:
        case sfx_bdcls:
        case sfx_pstop:
            fixfinder_play_sound(door_close, AMBIENCE);
            break;
        case sfx_swtchn:
        case sfx_swtchx:
        case sfx_oof:
        case sfx_noway:
            fixfinder_play_sound(click, AMBIENCE);
            break;
        case sfx_plpain:
            fixfinder_play_sound(pain, IMPACT);
            break;
        case sfx_pldeth:
        case sfx_pdiehi:
            fixfinder_play_sound(death, ALERT);
            break;
        }
    }
#else
    (void)id;
    (void)volume;
#endif
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
#ifdef FIXFINDER_DOOM_BOARD
    doom_board = v9_lcd_board_init();
    if (doom_board == NULL || !doom_board->landscape || doom_board->read_buttons == NULL) {
        I_Error("DOOM requires an initialized landscape display and buttons");
    }
    uint8_t controller_id;
    if (!hx8347_read_id(&doom_board->bus, &controller_id) ||
        controller_id != doom_board->expected_id) {
        I_Error("LCD controller ID check failed");
    }
#elif defined(FIXFINDER_DOOM_DCS)
    if (!doom_dcs_init()) {
        I_Error("FixFinder LCD ID check failed");
    }
    fixfinder_io_init();
#ifdef FIXFINDER_STORAGE_PROBE
    extern void fixfinder_emmc_probe(void);
    fixfinder_emmc_probe();
#endif
    fixfinder_beep(90);
#ifdef FIXFINDER_ARCADE
    fixfinder_arcade_run((uint8_t *)doom_frame, &milliseconds);
#endif
#endif
    setvbuf(stdout, NULL, _IONBF, 0);
    I_PreInitGraphics();
    Z_Init();
    InitGlobals();
    D_DoomMain();
}
