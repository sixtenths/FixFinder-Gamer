#ifndef FIXFINDER_EXMC_DCS_H
#define FIXFINDER_EXMC_DCS_H

#include <stdbool.h>
#include "gd32f4xx.h"
#include "gd32f4xx_exmc.h"
#include "gd32f4xx_gpio.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_timer.h"

#define LCD_INDEX REG8(0x68000000U)
#define LCD_DATA  REG8(0x68000001U)
static inline void configure_af(uint32_t port, uint32_t pins, unsigned int function)
{
    for (unsigned int pin = 0; pin < 16; ++pin) {
        if ((pins & (1U << pin)) == 0) {
            continue;
        }
        unsigned int shift = 2U * pin;
        unsigned int af_shift = 4U * (pin % 8U);
        volatile uint32_t *af = pin < 8 ? &GPIO_AFSEL0(port) : &GPIO_AFSEL1(port);
        *af = (*af & ~(15U << af_shift)) | (function << af_shift);
        GPIO_OMODE(port) &= ~(1U << pin);
        GPIO_OSPD(port) &= ~(3U << shift);
        GPIO_PUD(port) &= ~(3U << shift);
        GPIO_CTL(port) = (GPIO_CTL(port) & ~(3U << shift)) | (2U << shift);
    }
}

static inline uint32_t read_panel(uint8_t command)
{
    uint32_t bytes = 0;
    LCD_INDEX = command;
    for (unsigned int i = 0; i < 4; ++i) {
        bytes = (bytes << 8) | LCD_DATA;
    }
    return bytes;
}

static inline void start_backlight(void)
{
    /* Factory capture: TIMER10 CH0 PWM0, active high, approximately 60%. */
    RCU_APB2EN |= RCU_APB2EN_TIMER10EN;
    (void)RCU_APB2EN;
    TIMER_CTL0(TIMER10) = 0;
    TIMER_CHCTL2(TIMER10) = 0;
    TIMER_PSC(TIMER10) = 0;
    TIMER_CAR(TIMER10) = 0xffffU;
    TIMER_CH0CV(TIMER10) = 0x9999U;
    TIMER_CHCTL0(TIMER10) = TIMER_OC_MODE_PWM0;
    TIMER_SWEVG(TIMER10) = TIMER_SWEVG_UPG;
    TIMER_CHCTL2(TIMER10) = TIMER_CHCTL2_CH0EN;
    TIMER_CTL0(TIMER10) = TIMER_CTL0_ARSE | TIMER_CTL0_CEN;
    configure_af(GPIOF, 1U << 7, 3);
    GPIO_OSPD(GPIOF) |= 2U << 14;
}
static inline void write_data(uint8_t value)
{
    LCD_DATA = value;
    __DSB();
}

static inline void write_range(uint8_t command, uint16_t start, uint16_t end)
{
    LCD_INDEX = command;
    __DSB();
    write_data((uint8_t)(start >> 8));
    write_data((uint8_t)start);
    write_data((uint8_t)(end >> 8));
    write_data((uint8_t)end);
}

static inline void init_display_bus(void)
{
    RCU_AHB1EN |= RCU_AHB1EN_PDEN | RCU_AHB1EN_PEEN | RCU_AHB1EN_PFEN | RCU_AHB1EN_PGEN;
    RCU_AHB3EN |= RCU_AHB3EN_EXMCEN;
    (void)RCU_AHB3EN;
    EXMC_SNCTL2 = 0x00001080U;
    /* Extend the stock data phase for the first probe under V8's clock. */
    EXMC_SNTCFG2 = 0x00001f15U;
    configure_af(GPIOD, 0xc033U, 12);
    configure_af(GPIOE, 0x0780U, 12);
    configure_af(GPIOF, 0x0001U, 12);
    configure_af(GPIOG, 0x0400U, 12);
    /* Match the stock drive speed on NOE and NWE. */
    GPIO_OSPD(GPIOD) |= 0x00000f00U;
    EXMC_SNCTL2 = 0x00001081U;
    __DSB();
}

static inline bool panel_matches(void)
{
    return read_panel(0x04) == 0x00e30000U &&
           (read_panel(0xda) & 0x00ff0000U) == 0x00e30000U;
}

static inline void start_panel(void (*delay_ms)(uint32_t), uint8_t orientation)
{
    LCD_INDEX = 0x11;
    __DSB();
    delay_ms(150);
    LCD_INDEX = 0x3a;
    write_data(0x55);
    LCD_INDEX = 0x36;
    write_data(orientation);
    LCD_INDEX = 0x38;  /* Exit idle mode, which displays only eight colors. */
    __DSB();
    LCD_INDEX = 0x20;  /* Select non-inverted colors after a cold power-up. */
    __DSB();
    LCD_INDEX = 0x29;
    __DSB();
    delay_ms(30);
}

static inline void begin_panel_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    write_range(0x2a, x, x + width - 1U);
    write_range(0x2b, y, y + height - 1U);
    LCD_INDEX = 0x2c;
    __DSB();
}

static inline void begin_panel_frame(uint16_t width, uint16_t height)
{
    begin_panel_window(0, 0, width, height);
}

#endif
