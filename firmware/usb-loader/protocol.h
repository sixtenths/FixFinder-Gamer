#ifndef FIXFINDER_USB_LOADER_PROTOCOL_H
#define FIXFINDER_USB_LOADER_PROTOCOL_H

#include <stdint.h>

#define GAME_BASE 0x08008000U
#define GAME_LIMIT 0x08300000U
#define GAME_MANIFEST 0x08004000U
#define GAME_STACK_TOP 0x2002fff8U
#define BOOT_COOKIE ((volatile uint32_t *)GAME_STACK_TOP)
#define BOOT_GAME 0x53ac763bU
#define BOOT_LOADER 0x451bd692U

#define LOADER_REPORT_ID 0x41U
#define LOADER_REPORT_SIZE 64U
#define LOADER_PROTOCOL_VERSION 1U
#define LOADER_DATA_SIZE 56U

enum loader_command {
    LOADER_INFO = 1,
    LOADER_BEGIN,
    LOADER_WRITE,
    LOADER_COMMIT,
    LOADER_RUN
};

void loader_usb_arm(void);
void loader_usb_receive(uint32_t length);
void loader_usb_sent(void);

#endif
