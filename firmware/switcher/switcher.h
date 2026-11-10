#ifndef FIXFINDER_SWITCHER_H
#define FIXFINDER_SWITCHER_H

#include "../usb-loader/protocol.h"
#include "gd32f4xx.h"
#include <stdbool.h>

/* The USB loader verifies only the permanent switcher at GAME_BASE.
 * Sector 4 starts with the local slot's commit record. */
#define SWITCH_SLOT 0x08010000U
#define SWITCH_APP 0x08010200U
#define SWITCH_LIMIT 0x082c0000U
#define SWITCH_MAGIC 0x31534746U
#define SWITCH_COOKIE ((volatile uint32_t *)0x1000fff8U)
#define SWITCH_SEED_STATUS 0x53454544U
#define SWITCH_SEED_ERROR 0x53450000U

enum switch_game { SWITCH_ARCADE = 1, SWITCH_SONIC = 2 };
enum { SWITCH_RUN = 0x100U };

struct switch_image {
    uint32_t magic, game, size, crc;
};

static inline bool switch_game_known(uint32_t game)
{
    return game == SWITCH_ARCADE || game == SWITCH_SONIC;
}

static inline bool switch_image_ok(const struct switch_image *image, uint32_t game)
{
    return image->magic == SWITCH_MAGIC && image->game == game &&
           image->size >= 8 && image->size <= SWITCH_LIMIT - SWITCH_APP && !(image->size & 3);
}

static inline bool switch_vectors_ok(uint32_t stack, uint32_t entry, uint32_t size)
{
    return stack > 0x20000000U && stack <= GAME_STACK_TOP && !(stack & 7U) &&
           (entry & 1U) && entry >= SWITCH_APP + 9U && entry < SWITCH_APP + size;
}

__attribute__((noreturn)) static inline void switch_reset(uint32_t request, uint32_t boot)
{
    __disable_irq();
    SWITCH_COOKIE[0] = request;
    SWITCH_COOKIE[1] = ~request;
    BOOT_COOKIE[0] = boot;
    BOOT_COOKIE[1] = ~boot;
    NVIC_SystemReset();
    __builtin_unreachable();
}

bool switch_package_available(uint32_t game);

#endif
