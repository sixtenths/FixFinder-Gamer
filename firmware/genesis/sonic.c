/* FixFinder frontend for Gwenesis. SPDX-License-Identifier: AGPL-3.0-or-later */
#include "../game-sdk/fixfinder.h"
#include "../doom-swd/display.h"
#include "../doom-swd/input.h"
#include "../doom-swd/usb_input.h"
#include "../doom-swd/usb_video.h"
#include "m68k.h"
#include "gwenesis_bus.h"
#include "gwenesis_io.h"
#undef BIT
#undef BITS
#include "gwenesis_vdp.h"
#include "gwenesis_sn76489.h"
#include "ym2612.h"
#include "z80inst.h"
#include <string.h>

enum { LCD_WIDTH = 320, LCD_HEIGHT = 240, BLACK = 192 };
int system_clock, scan_line;
int ym2612_clock, ym2612_index, sn76489_clock, sn76489_index;
int16_t gwenesis_ym2612_buffer[GWENESIS_AUDIO_BUFFER_LENGTH_PAL];
int16_t gwenesis_sn76489_buffer[GWENESIS_AUDIO_BUFFER_LENGTH_PAL];
uint8_t fixfinder_genesis_line[LCD_WIDTH];
void fixfinder_genesis_rebase(unsigned int cycles);
unsigned int fixfinder_genesis_psg_hz(void);
extern int screen_width, screen_height, hint_pending;
extern uint16_t gwenesis_vdp_status, CRAM[64];
extern uint8_t gwenesis_vdp_regs[32];

static uint16_t previous_cram[64];
static uint8_t palette[768];
static uint8_t blank[LCD_WIDTH];
static uint32_t frame_count, previous_keys;
volatile uint32_t fixfinder_genesis_frames;
volatile uint32_t fixfinder_genesis_render_ms;
volatile uint32_t fixfinder_genesis_step_ms;

void gwenesis_io_get_buttons(void)
{
    /* The platform polls once per emulated frame. */
}

static void poll_pad(void)
{
    uint32_t physical = fixfinder_physical_keys(fixfinder_millis());
    uint32_t remote = doom_usb_input_buttons();
    bool start = (physical & (KEY_BACK | KEY_ENTER)) == (KEY_BACK | KEY_ENTER);
    bool left = (!start && (physical & KEY_BACK)) || (remote & DOOM_BUTTON_LEFT);
    bool right = (!start && (physical & KEY_ENTER)) || (remote & DOOM_BUTTON_RIGHT);
    bool jump = (physical & KEY_ERASE) || (remote & DOOM_BUTTON_FIRE);
    bool pressed[] = {
        [PAD_UP] = (remote & DOOM_BUTTON_FORWARD) != 0,
        [PAD_DOWN] = (physical & KEY_DOWN) || (remote & DOOM_BUTTON_BACK),
        [PAD_LEFT] = left,
        [PAD_RIGHT] = right,
        [PAD_B] = jump,
        [PAD_S] = start || (remote & (DOOM_BUTTON_USE | DOOM_BUTTON_MENU)),
    };
    uint32_t keys = 0;
    for (unsigned int i = 0; i < sizeof pressed / sizeof *pressed; ++i) {
        uint32_t bit = 1U << i;
        if (pressed[i]) {
            keys |= bit;
            if (!(previous_keys & bit)) gwenesis_io_pad_press_button(0, i);
        } else if (previous_keys & bit) {
            gwenesis_io_pad_release_button(0, i);
        }
    }
    previous_keys = keys;
}

static void update_palette(void)
{
    if (!memcmp(previous_cram, CRAM, sizeof previous_cram)) return;
    memcpy(previous_cram, CRAM, sizeof previous_cram);
    for (unsigned int i = 0; i < 64; ++i) {
        unsigned int color = CRAM[i];
        unsigned int rgb[] = {CRAM_R(color), CRAM_G(color), CRAM_B(color)};
        for (unsigned int component = 0; component < 3; ++component) {
            palette[3*i + component] = rgb[component];
            palette[3*(i + 64) + component] = rgb[component] >> 1;
            palette[3*(i + 128) + component] = (rgb[component] >> 1) | 0x80;
        }
    }
    doom_display_set_palette(palette);
}

static unsigned int preview_y;

static void output_row(unsigned int y, const uint8_t *pixels, bool lcd, bool native, bool preview)
{
    if (lcd) doom_dcs_stream_native_row(y, pixels);
    if (native) doom_usb_video_native_row(y, pixels);
    if (preview && preview_y < FIXFINDER_HEIGHT && preview_y * LCD_HEIGHT / FIXFINDER_HEIGHT == y) {
        uint8_t *dest = fixfinder_screen + preview_y++ * FIXFINDER_WIDTH;
        for (unsigned int x = 0; x < FIXFINDER_WIDTH; ++x)
            dest[x] = pixels[x * LCD_WIDTH / FIXFINDER_WIDTH];
    }
}

static void emulate_frame(bool lcd)
{
    screen_height = REG1_240_LINE ? 240 : 224;
    screen_width = REG12_MODE_H40 ? 320 : 256;
    unsigned int border = (LCD_HEIGHT - screen_height) / 2;
    gwenesis_vdp_render_config();
    system_clock = zclk = 0;
    ym2612_clock = ym2612_index = sn76489_clock = sn76489_index = 0;
    int hint = REG10_LINE_COUNTER;
    bool capture = doom_usb_video_native_requested();
    bool preview = doom_usb_video_preview_requested();
    bool output = lcd || capture || preview;
    preview_y = 0;
    if (output) {
        update_palette();
        if (lcd) doom_dcs_begin_native();
        if (capture) doom_usb_video_native_begin(palette);
        for (unsigned int y = 0; y < border; ++y) {
            output_row(y, blank, lcd, capture, preview);
        }
    }
    for (scan_line = 0; scan_line < LINES_PER_FRAME_NTSC; ++scan_line) {
        int target = system_clock + VDP_CYCLES_PER_LINE;
        m68k_run(target);
        z80_run(target);
        unsigned int y = border + scan_line;
        if (scan_line < screen_height &&
            (lcd || preview || (capture && doom_usb_video_native_wants_row(y)))) {
            gwenesis_vdp_render_line(scan_line);
            update_palette();
            output_row(y, fixfinder_genesis_line, lcd, capture, preview);
        }
        if (scan_line == 0 || scan_line > screen_height) hint = REG10_LINE_COUNTER;
        if (--hint < 0) {
            if (REG0_LINE_INTERRUPT && scan_line <= screen_height) {
                hint_pending = 1;
                if (!(gwenesis_vdp_status & STATUS_VIRQPENDING)) m68k_update_irq(4);
            }
            hint = REG10_LINE_COUNTER;
        }
        if (scan_line + 1 == screen_height) {
            if (REG1_VBLANK_INTERRUPT) {
                gwenesis_vdp_status |= STATUS_VIRQPENDING;
                m68k_set_irq(6);
            }
            z80_irq_line(1);
        }
        if (scan_line == screen_height) z80_irq_line(0);
        system_clock = target;
    }
    if (output) {
        for (unsigned int y = LCD_HEIGHT - border; y < LCD_HEIGHT; ++y) {
            output_row(y, blank, lcd, capture, preview);
        }
        if (lcd) doom_dcs_finish_native_stream();
        if (capture) doom_usb_video_native_end();
        if (preview) doom_usb_video_capture(fixfinder_screen, palette);
    }
    fixfinder_genesis_rebase(system_clock);
}

void fixfinder_game_main(void)
{
    memset(blank, BLACK, sizeof blank);
    memset(previous_cram, 0xff, sizeof previous_cram);
    power_on();
    load_cartridge(NULL, 0);
    reset_emulation();
    fixfinder_set_led(FIXFINDER_LED_GREEN);

    uint32_t deadline = fixfinder_millis();
    for (;;) {
        uint32_t start = fixfinder_millis();
        poll_pad();
        bool lcd = frame_count % 3 == 0;
        emulate_frame(lcd);
        uint32_t work_ms = fixfinder_millis() - start;
        if (lcd) fixfinder_genesis_render_ms = work_ms;
        else fixfinder_genesis_step_ms = work_ms;
        fixfinder_tone(fixfinder_genesis_psg_hz());
        fixfinder_genesis_frames = ++frame_count;
        deadline += frame_count % 3 ? 17 : 16;
        int32_t remaining = (int32_t)(deadline - fixfinder_millis());
        if (remaining > 0) fixfinder_sleep(remaining);
        else if (remaining < -100) deadline = fixfinder_millis();
    }
}
