#include "fixfinder_io.h"
#include "../doom-swd/input.h"
#ifdef FIXFINDER_USB_VIDEO
#include "../doom-swd/usb_input.h"
#endif
#include "gd32f4xx.h"
#include "gd32f4xx_gpio.h"
#include "gd32f4xx_rcu.h"
#include "gd32f4xx_timer.h"
#include <stdbool.h>
#include <stddef.h>

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
static enum fixfinder_led active_led;
static uint32_t tone_hz, beeper_clock, tone_prescaler;
static bool tone_high;
static int16_t (*volatile pcm_sample)(void);
static uint32_t pcm_error;

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
    active_led = FIXFINDER_LED_OFF;
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
    int16_t (*sample)(void) = pcm_sample;
    if (sample) {
        /* First-order pulse density modulation for the one-bit GPIO output. */
        pcm_error += 512 + sample();
        GPIO_BOP(GPIOF) = pcm_error >= 1024 ? BEEPER_PIN : BEEPER_PIN << 16;
        pcm_error &= 1023U;
    } else {
        tone_high = !tone_high;
        GPIO_BOP(GPIOF) = tone_high ? BEEPER_PIN : BEEPER_PIN << 16;
    }
}

static void init_beeper_timer(void)
{
    if (!beeper_clock) {
        /* TIMER6 is on APB1. Its clock multiplier depends on TIMERSEL. */
        unsigned int prescaler = (RCU_CFG0 & RCU_CFG0_APB1PSC) >> 10;
        unsigned int apb_div = prescaler < 4 ? 1U : 2U << (prescaler - 4);
        unsigned int timer_mul = (RCU_CFG1 & RCU_CFG1_TIMERSEL) ? 4U : 2U;
        beeper_clock = SystemCoreClock / (apb_div > timer_mul ? apb_div / timer_mul : 1U);
        tone_prescaler = (beeper_clock + 999999U) / 1000000U - 1;

        RCU_APB1EN |= RCU_APB1EN_TIMER6EN;
        (void)RCU_APB1EN;
        TIMER_CTL0(TIMER6) = TIMER_CTL0_ARSE;
        NVIC_SetPriority(TIMER6_IRQn, 8);
        NVIC_EnableIRQ(TIMER6_IRQn);
    }
}

static void start_beeper_timer(uint32_t prescaler, uint32_t reload)
{
    beep_remaining = 0;
    tone_high = false;
    pcm_error = 0;
    GPIO_BOP(GPIOF) = BEEPER_PIN << 16;
    TIMER_PSC(TIMER6) = prescaler;
    TIMER_CAR(TIMER6) = reload;
    TIMER_SWEVG(TIMER6) = TIMER_SWEVG_UPG;
    TIMER_INTF(TIMER6) = 0;
    TIMER_DMAINTEN(TIMER6) = TIMER_DMAINTEN_UPIE;
    TIMER_CTL0(TIMER6) |= TIMER_CTL0_CEN;
}

static void stop_beeper_timer(void)
{
    TIMER_DMAINTEN(TIMER6) = 0;
    TIMER_CTL0(TIMER6) &= ~TIMER_CTL0_CEN;
    NVIC_ClearPendingIRQ(TIMER6_IRQn);
    GPIO_BOP(GPIOF) = BEEPER_PIN << 16;
}

void fixfinder_pcm_stop(void)
{
    if (!pcm_sample) return;
    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    stop_beeper_timer();
    pcm_sample = NULL;
    __set_PRIMASK(interrupt_mask);
}

void fixfinder_pcm_start(int16_t (*sample)(void))
{
    if (pcm_sample == sample) return;
    fixfinder_pcm_stop();
    if (!sample) return;
    fixfinder_tone(0);
    init_beeper_timer();
    uint32_t interrupt_mask = __get_PRIMASK();
    __disable_irq();
    pcm_sample = sample;
    start_beeper_timer(0, (beeper_clock + FIXFINDER_PCM_RATE / 2) / FIXFINDER_PCM_RATE - 1);
    __set_PRIMASK(interrupt_mask);
}

void fixfinder_tone(uint32_t hz)
{
    if (hz == tone_hz) return;

    if (hz) {
        fixfinder_pcm_stop();
        init_beeper_timer();
        uint32_t reload = (beeper_clock / (tone_prescaler + 1) + hz) / (2U * hz) - 1;
        if (!tone_hz) {
            uint32_t interrupt_mask = __get_PRIMASK();
            __disable_irq();
            start_beeper_timer(tone_prescaler, reload);
            __set_PRIMASK(interrupt_mask);
        } else {
            TIMER_CAR(TIMER6) = reload;
        }
    } else {
        stop_beeper_timer();
    }
    tone_hz = hz;
}

void fixfinder_set_led(enum fixfinder_led led)
{
    if (led == active_led) return;
    GPIO_BOP(GPIOG) = (led == FIXFINDER_LED_GREEN ? GREEN_PIN << 16 : GREEN_PIN) |
                      (led == FIXFINDER_LED_YELLOW ? YELLOW_PIN << 16 : YELLOW_PIN);
    GPIO_BOP(GPIOF) = led == FIXFINDER_LED_RED ? RED_PIN << 16 : RED_PIN;
    active_led = led;
}

void fixfinder_health_led(int health)
{
    fixfinder_set_led(health > 50 ? FIXFINDER_LED_GREEN :
                      health > 25 ? FIXFINDER_LED_YELLOW : FIXFINDER_LED_RED);
}

uint32_t fixfinder_physical_keys(uint32_t now)
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

uint32_t fixfinder_keys(uint32_t now)
{
    uint32_t physical = fixfinder_physical_keys(now);
#ifdef FIXFINDER_USB_VIDEO
    uint32_t remote = doom_usb_input_keys();
    if (remote & USB_INPUT_RETURN) return KEY_DOWN | KEY_BACK;
    if (remote) return remote;
#endif
    return physical;
}

uint32_t fixfinder_buttons(uint32_t now)
{
    uint32_t stable = fixfinder_physical_keys(now);
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
