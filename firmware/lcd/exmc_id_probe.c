#include "exmc_dcs.h"

#define PROBE_RESULT ((volatile uint32_t *)0x20011000U)

static void data_pull(uint32_t pull)
{
    /* D0–D3 are PD14, PD15, PD0 and PD1. D4–D7 are PE7–PE10. */
    GPIO_PUD(GPIOD) = (GPIO_PUD(GPIOD) & ~0xf000000fU) | (pull * 0x50000005U);
    GPIO_PUD(GPIOE) = (GPIO_PUD(GPIOE) & ~0x003fc000U) | (pull * 0x00154000U);
}

#ifdef FIXFINDER_PROBE_GREEN
static void settle(uint32_t milliseconds)
{
    uint32_t cycles = milliseconds * 200000U;
    while (cycles-- != 0) {
        __NOP();
    }
}

static void show_green(void)
{
    /* Use DCS commands shared by this panel family. Keep vendor power and
     * gamma registers at their defaults. No hardware reset GPIO is needed. */
    if (!panel_matches()) {
        PROBE_RESULT[0] = 0x4c4344ffU;
        return;
    }
    PROBE_RESULT[0] = 0x4c434403U;
#ifdef FIXFINDER_PROBE_BACKLIGHT
    start_backlight();
#endif
#if defined(FIXFINDER_PROBE_RAMP)
    start_panel(settle, 0xa0);
#ifdef FIXFINDER_PROBE_RGB666
    LCD_INDEX = 0x3a;
    write_data(0x66);
#endif
    begin_panel_frame(320, 240);
    for (unsigned int row = 0; row < 240; ++row) {
        for (unsigned int x = 0; x < 320; ++x) {
#ifdef FIXFINDER_PROBE_RGB666
            uint8_t value = (uint8_t)((x / 5) << 2);
            write_data(row < 120 ? value : 0);
            write_data(row < 60 || (row >= 120 && row < 180) ? value : 0);
            write_data(row < 60 || row >= 180 ? value : 0);
#else
            uint16_t r = (uint16_t)(x / 10), g = (uint16_t)(x / 5);
            uint16_t color = row < 60 ? (r << 11) | (g << 5) | r :
                             row < 120 ? r << 11 : row < 180 ? g << 5 : r;
            write_data((uint8_t)(color >> 8));
            write_data((uint8_t)color);
#endif
        }
    }
#elif defined(FIXFINDER_PROBE_BARS)
    static const uint16_t bars[] = {0xffff, 0xf800, 0x07e0, 0x001f, 0x0000, 0x07ff, 0xf81f, 0xffe0};
    start_panel(settle, 0xa0);
    begin_panel_frame(320, 240);
    for (unsigned int row = 0; row < 240; ++row) {
        uint16_t color = bars[row / 30];
        for (unsigned int x = 0; x < 320; ++x) {
            write_data((uint8_t)(color >> 8));
            write_data((uint8_t)color);
        }
    }
#else
    start_panel(settle, 0);
    begin_panel_frame(240, 320);
    for (unsigned int i = 0; i < 240U * 320U; ++i) {
        write_data(0x07);
        write_data(0xe0);
    }
#endif
    PROBE_RESULT[32] = read_panel(0x0a);
    PROBE_RESULT[33] = read_panel(0x0c);
    PROBE_RESULT[34] = read_panel(0x2e);
    PROBE_RESULT[0] = 0x4c434404U;
}
#endif

__attribute__((noreturn, section(".text.probe_entry"))) void probe_entry(void)
{
    static const uint8_t indices[] = {0x00, 0x04, 0x09, 0x0a, 0x0c, 0xd3, 0xda, 0xdb, 0xdc};

    __disable_irq();
    PROBE_RESULT[0] = 0x4c434400U;
    init_display_bus();
    PROBE_RESULT[0] = 0x4c434401U;

    for (uint32_t pull = 0; pull < 3; ++pull) {
        data_pull(pull);
        for (unsigned int index = 0; index < sizeof indices; ++index) {
            PROBE_RESULT[1U + pull * sizeof indices + index] = read_panel(indices[index]);
        }
    }
    data_pull(0);
    PROBE_RESULT[0] = 0x4c434402U;
#ifdef FIXFINDER_PROBE_GREEN
    show_green();
#endif
    for (;;) {
        __NOP();
    }
}
