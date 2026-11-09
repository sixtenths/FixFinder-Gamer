#include "radio_frame.h"
#include "../lcd/exmc_dcs.h"
#include "gd32f4xx_usart.h"

#if defined(FIXFINDER_RADIO_UART3)
#define RADIO_UART UART3
#define RADIO_CLOCK CK_APB1
#define RADIO_PORT_PINS ((1U << 0) | (1U << 1))
#define RADIO_AF 8
#elif defined(FIXFINDER_RADIO_USART0)
#define RADIO_UART USART0
#define RADIO_CLOCK CK_APB2
#define RADIO_PORT_PINS ((1U << 9) | (1U << 10))
#define RADIO_AF 7
#endif

bool radio_requested(void)
{
    RCU_AHB1EN |= RCU_AHB1EN_PFEN;
    (void)RCU_AHB1EN;
    GPIO_CTL(GPIOF) &= ~(3U << 2);
    GPIO_PUD(GPIOF) = (GPIO_PUD(GPIOF) & ~(3U << 2)) | (1U << 2);
    return !(GPIO_ISTAT(GPIOF) & (1U << 1));
}

void radio_init(void)
{
    RCU_AHB1EN |= RCU_AHB1EN_PAEN;
#if defined(FIXFINDER_RADIO_UART3)
    RCU_APB1EN |= RCU_APB1EN_UART3EN;
    (void)RCU_APB1EN;
#else
    RCU_APB2EN |= RCU_APB2EN_USART0EN;
    (void)RCU_APB2EN;
#endif
    configure_af(GPIOA, RADIO_PORT_PINS, RADIO_AF);
    USART_CTL0(RADIO_UART) = 0;
    USART_BAUD(RADIO_UART) = (rcu_clock_freq_get(RADIO_CLOCK) + RADIO_BAUD / 2) / RADIO_BAUD;
    USART_CTL0(RADIO_UART) = USART_CTL0_UEN | USART_CTL0_TEN | USART_CTL0_REN;
}

static void send_byte(uint8_t byte)
{
    while (!(USART_STAT0(RADIO_UART) & USART_STAT0_TBE)) {}
    USART_DATA(RADIO_UART) = byte;
}

void radio_wake(void)
{
    send_byte(RADIO_MARK);
    send_byte(RADIO_WAKE);
}

void radio_send(const uint8_t *report)
{
    uint16_t crc = radio_crc(report);
    send_byte(RADIO_MARK);
    send_byte(RADIO_FRAME);
    for (unsigned int i = 0; i < LOADER_REPORT_SIZE; ++i) {
        send_byte(report[i]);
    }
    send_byte((uint8_t)crc);
    send_byte((uint8_t)(crc >> 8));
    while (!(USART_STAT0(RADIO_UART) & USART_STAT0_TC)) {}
}

bool radio_poll(uint8_t *report, uint32_t now)
{
    static unsigned int state;
    static uint16_t crc;
    static uint32_t received_at;
    uint32_t status = USART_STAT0(RADIO_UART);
    if (now - received_at > SystemCoreClock / 20) {
        state = 0;
    }
    if (!(status & USART_STAT0_RBNE)) {
        return false;
    }
    uint8_t byte = USART_DATA(RADIO_UART);
    received_at = now;
    if (status & (USART_STAT0_ORERR | USART_STAT0_FERR | USART_STAT0_NERR)) {
        state = 0;
        return false;
    }
    if (state < 2) {
        state = (state == 1 && byte == RADIO_FRAME) ? 2 : (byte == RADIO_MARK);
    } else if (state < 2 + LOADER_REPORT_SIZE) {
        report[state++ - 2] = byte;
    } else if (state == 2 + LOADER_REPORT_SIZE) {
        crc = byte;
        state++;
    } else {
        state = 0;
        return (crc | ((uint16_t)byte << 8)) == radio_crc(report);
    }
    return false;
}
