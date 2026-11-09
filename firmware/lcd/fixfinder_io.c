#include "fixfinder_io.h"
#include "../doom-swd/input.h"
#include "gd32f4xx.h"
#include "gd32f4xx_gpio.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_timer.h"
#include <stdbool.h>

#define BACK_PIN  (1U << 0)  /* PG0 */
#define DOWN_PIN  (1U << 4)  /* PF4 */
#define ENTER_PIN (1U << 1)  /* PF1 */
#define ERASE_PIN (1U << 5)  /* PF5 */
#define F_MODE_MASK ((3U << 2) | (3U << 8) | (3U << 10))
#define F_PULL_UP ((1U << 2) | (1U << 8) | (1U << 10))
#define GREEN_PIN (1U << 12)  /* PG12, active low */
#define YELLOW_PIN (1U << 13) /* PG13, active low */
#define RED_PIN (1U << 14)    /* PF14, active low */
#define BEEPER_PIN (1U << 13) /* PF13, active high */

static volatile uint32_t beep_remaining;
static uint32_t tone_hz, tone_timer_hz;
static bool tone_high;

static void configure_output(uint32_t port, unsigned int pin, unsigned int high)
{
    const unsigned int shift = pin * 2;
    GPIO_BOP(port) = 1U << (high ? pin : pin + 16);
    GPIO_OMODE(port) &= ~(1U << pin);
    GPIO_PUD(port) &= ~(3U << shift);
    GPIO_CTL(port) = (GPIO_CTL(port) & ~(3U << shift)) | (1U << shift);
}

void fixfinder_io_init(void)
{
    RCU_AHB1EN |= RCU_AHB1EN_PFEN | RCU_AHB1EN_PGEN;
    (void)RCU_AHB1EN;
    GPIO_CTL(GPIOF) &= ~F_MODE_MASK;
    GPIO_PUD(GPIOF) = (GPIO_PUD(GPIOF) & ~F_MODE_MASK) | F_PULL_UP;
    GPIO_CTL(GPIOG) &= ~3U;
    GPIO_PUD(GPIOG) = (GPIO_PUD(GPIOG) & ~3U) | 1U;
    configure_output(GPIOG, 12, 1);
    configure_output(GPIOG, 13, 1);
    configure_output(GPIOF, 14, 1);
    configure_output(GPIOF, 13, 0);
}

void fixfinder_beep(uint32_t duration_ms)
{
    if (duration_ms == 0) {
        return;
    }
    const uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    if (beep_remaining < duration_ms) {
        beep_remaining = duration_ms;
    }
    GPIO_BOP(GPIOF) = BEEPER_PIN;
    __set_PRIMASK(interrupt_mask);
}

void fixfinder_io_tick(void)
{
    if (beep_remaining != 0 && --beep_remaining == 0) {
        GPIO_BOP(GPIOF) = BEEPER_PIN << 16;
    }
}

void TIMER6_IRQHandler(void)
{
    TIMER_INTF(TIMER6) = 0;
    tone_high = !tone_high;
    GPIO_BOP(GPIOF) = tone_high ? BEEPER_PIN : BEEPER_PIN << 16;
}

void fixfinder_tone(uint32_t hz)
{
    if (hz == tone_hz) return;

    if (hz && !tone_timer_hz) {
        /* TIMER6 is on APB1. Its clock multiplier depends on TIMERSEL. */
        unsigned int prescaler = (RCU_CFG0 & RCU_CFG0_APB1PSC) >> 10;
        unsigned int apb_div = prescaler < 4 ? 1U : 2U << (prescaler - 4);
        unsigned int timer_mul = (RCU_CFG1 & RCU_CFG1_TIMERSEL) ? 4U : 2U;
        uint32_t timer_hz = SystemCoreClock / (apb_div > timer_mul ? apb_div / timer_mul : 1U);
        uint32_t divider = (timer_hz + 999999U) / 1000000U;
        tone_timer_hz = timer_hz / divider;

        RCU_APB1EN |= RCU_APB1EN_TIMER6EN;
        (void)RCU_APB1EN;
        TIMER_CTL0(TIMER6) = TIMER_CTL0_ARSE;
        TIMER_PSC(TIMER6) = divider - 1;
        NVIC_SetPriority(TIMER6_IRQn, 8);
        NVIC_EnableIRQ(TIMER6_IRQn);
    }

    if (hz) {
        TIMER_CAR(TIMER6) = (tone_timer_hz + hz) / (2U * hz) - 1;
        if (!tone_hz) {
            uint32_t interrupt_mask = __get_PRIMASK();
            __disable_irq();
            beep_remaining = 0;
            tone_high = false;
            GPIO_BOP(GPIOF) = BEEPER_PIN << 16;
            TIMER_SWEVG(TIMER6) = TIMER_SWEVG_UPG;
            TIMER_INTF(TIMER6) = 0;
            TIMER_DMAINTEN(TIMER6) = TIMER_DMAINTEN_UPIE;
            TIMER_CTL0(TIMER6) |= TIMER_CTL0_CEN;
            __set_PRIMASK(interrupt_mask);
        }
    } else {
        TIMER_DMAINTEN(TIMER6) = 0;
        TIMER_CTL0(TIMER6) &= ~TIMER_CTL0_CEN;
        NVIC_ClearPendingIRQ(TIMER6_IRQn);
        GPIO_BOP(GPIOF) = BEEPER_PIN << 16;
    }
    tone_hz = hz;
}

void fixfinder_health_led(int health)
{
    if (health > 50) {
        GPIO_BOP(GPIOG) = (GREEN_PIN << 16) | YELLOW_PIN;
        GPIO_BOP(GPIOF) = RED_PIN;
    } else if (health > 25) {
        GPIO_BOP(GPIOG) = GREEN_PIN | (YELLOW_PIN << 16);
        GPIO_BOP(GPIOF) = RED_PIN;
    } else {
        GPIO_BOP(GPIOG) = GREEN_PIN | YELLOW_PIN;
        GPIO_BOP(GPIOF) = RED_PIN << 16;
    }
}

uint32_t fixfinder_keys(uint32_t now)
{
    static uint32_t candidate, stable, changed_at;
    uint32_t f = ~GPIO_ISTAT(GPIOF);
    uint32_t g = ~GPIO_ISTAT(GPIOG);
    uint32_t pressed = ((g & BACK_PIN) ? KEY_BACK : 0) |
                       ((f & DOWN_PIN) ? KEY_DOWN : 0) |
                       ((f & ENTER_PIN) ? KEY_ENTER : 0) |
                       ((f & ERASE_PIN) ? KEY_ERASE : 0);
    if (pressed != candidate) {
        candidate = pressed;
        changed_at = now;
    } else if (now - changed_at >= 16U) {
        stable = candidate;
    }

    return stable;
}

uint32_t fixfinder_buttons(uint32_t now)
{
    uint32_t stable = fixfinder_keys(now);
    switch (stable) {
    case KEY_BACK | KEY_ENTER: return DOOM_BUTTON_FIRE;
    case KEY_ERASE | KEY_DOWN: return DOOM_BUTTON_USE;
    case KEY_DOWN | KEY_BACK: return DOOM_BUTTON_MENU;
    case KEY_DOWN | KEY_ENTER: return DOOM_BUTTON_MAP;
    default:
        return ((stable & KEY_BACK) ? DOOM_BUTTON_LEFT : 0) |
               ((stable & KEY_DOWN) ? DOOM_BUTTON_BACK : 0) |
               ((stable & KEY_ENTER) ? DOOM_BUTTON_RIGHT : 0) |
               ((stable & KEY_ERASE) ? DOOM_BUTTON_FORWARD : 0);
    }
}
