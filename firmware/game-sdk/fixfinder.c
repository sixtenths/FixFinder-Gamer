#include "fixfinder.h"
#include "../doom-swd/display.h"
#include "../doom-swd/usb_input.h"
#include "../doom-swd/usb_video.h"
#ifdef FIXFINDER_ARCADE_ONLY
#include "../doom-swd/arcade.h"
#endif
#include "gd32f4xx.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>

static volatile uint32_t milliseconds;
uint8_t fixfinder_screen[FIXFINDER_WIDTH * FIXFINDER_HEIGHT]
    __attribute__((section(".doom_frame")));
static uint8_t palette[FIXFINDER_PALETTE_SIZE];

bool doom_dcs_init(void);
void doom_dcs_present(const uint8_t *frame);

void SysTick_Handler(void)
{
    ++milliseconds;
    doom_usb_input_tick();
    fixfinder_io_tick();
}

uint32_t fixfinder_millis(void)
{
    return milliseconds;
}

void fixfinder_sleep(uint32_t ms)
{
    uint32_t start = milliseconds;
    while (milliseconds - start < ms) {
        __WFI();
    }
}

void fixfinder_palette(const uint8_t rgb[FIXFINDER_PALETTE_SIZE])
{
    memcpy(palette, rgb, sizeof palette);
    doom_display_set_palette(palette);
}

void fixfinder_present(void)
{
    doom_dcs_present(fixfinder_screen);
    doom_usb_video_capture(fixfinder_screen, palette);
}

void doom_lcd_write_row(unsigned int row, const uint16_t *pixels)
{
    (void)row;
    (void)pixels;
}

void *_sbrk(ptrdiff_t increment)
{
    extern char _ebss, __heap_end;
    const uintptr_t start = (uintptr_t)&_ebss;
    const uintptr_t limit = (uintptr_t)&__heap_end;
    static uintptr_t current;
    if (!current) current = start;
    if (increment > (ptrdiff_t)(limit - current) ||
        increment < -(ptrdiff_t)(current - start)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    uintptr_t previous = current;
    current += increment;
    return (void *)previous;
}

void fixfinder_game_run(void)
{
    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) || !doom_dcs_init()) return;
    fixfinder_io_init();
    /* TCM sections are NOLOAD. Initialize the public framebuffer explicitly. */
    memset(fixfinder_screen, 0, sizeof fixfinder_screen);
#ifdef FIXFINDER_ARCADE_ONLY
    fixfinder_arcade_run(fixfinder_screen, &milliseconds);
#else
    fixfinder_game_main();
#endif
}
