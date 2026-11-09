#include <stdbool.h>
#include <string.h>
#include "drv_usb_hw.h"
#include "custom_hid_core.h"
#include "gd32f4xx_fmc.h"
#include "protocol.h"
#include "screen.h"

#define MANIFEST_MAGIC 0x31474646U
#define BOOT_GAME_LIT 0x726d4bf3U
#define FLASH_ERRORS (FMC_FLAG_END | FMC_FLAG_OPERR | FMC_FLAG_WPERR | \
                      FMC_FLAG_PGMERR | FMC_FLAG_PGSERR | FMC_FLAG_RDDERR)

struct manifest {
    uint32_t magic;
    uint32_t size;
    uint32_t crc;
};

static const volatile struct manifest *const manifest = (void *)GAME_MANIFEST;
static uint8_t rx[LOADER_REPORT_SIZE] __attribute__((aligned(4)));
static uint8_t tx[LOADER_REPORT_SIZE] __attribute__((aligned(4)));
static volatile bool pending;
static volatile bool run_acknowledged;
static volatile bool run_requested;
static bool valid_game;
static bool screen_available;
#ifdef FIXFINDER_RADIO
static bool radio_mode;
bool radio_requested(void);
void radio_init(void);
void radio_wake(void);
void radio_send(const uint8_t *report);
bool radio_poll(uint8_t *report, uint32_t now);
#endif
static uint32_t size, expected_crc, cursor, erased_until;

usb_core_driver custom_hid;

static uint32_t get_word(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_word(uint8_t *p, uint32_t value)
{
    for (unsigned int i = 0; i < 4; ++i) {
        p[i] = (uint8_t)(value >> (8 * i));
    }
}

static uint32_t game_crc(uint32_t count)
{
    const uint8_t *bytes = (const uint8_t *)GAME_BASE;
    uint32_t crc = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        crc ^= bytes[i];
        for (unsigned int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

static bool size_ok(uint32_t count)
{
    return count >= 8 && count <= GAME_LIMIT - GAME_BASE && (count & 3U) == 0;
}

static bool vectors_ok(uint32_t count)
{
    const volatile uint32_t *vectors = (const uint32_t *)GAME_BASE;
    uint32_t stack = vectors[0], entry = vectors[1];
    return stack > 0x20000000U && stack <= GAME_STACK_TOP && (stack & 7U) == 0 &&
           (entry & 1U) && (entry & ~1U) >= GAME_BASE + 8 &&
           (entry & ~1U) < GAME_BASE + count;
}

static bool installed_game_valid(void)
{
    return manifest->magic == MANIFEST_MAGIC && size_ok(manifest->size) &&
           vectors_ok(manifest->size) && game_crc(manifest->size) == manifest->crc;
}

static bool erase_sector(uint32_t number)
{
    fmc_unlock();
    fmc_flag_clear(FLASH_ERRORS);
    FMC_CTL = (FMC_CTL & ~FMC_CTL_PSZ) | CTL_PSZ_WORD;
    bool ok = fmc_sector_erase(CTL_SN(number)) == FMC_READY;
    fmc_lock();
    return ok;
}

/* Sector numbers below are FMC selector values, including the bank bit. */
static bool erase_next(void)
{
    uint32_t offset = erased_until - 0x08000000U;
    uint32_t bank_base = offset < 0x100000U ? 0 : 0x100000U;
    uint32_t index = offset - bank_base;
    uint32_t selector = bank_base ? 16 : 0;
    uint32_t length;
    if (offset >= 0x200000U) {
        selector = 12 + (offset - 0x200000U) / 0x40000U;
        length = 0x40000U;
    } else if (index < 0x10000U) {
        selector += index / 0x4000U;
        length = 0x4000U;
    } else if (index == 0x10000U) {
        selector += 4;
        length = 0x10000U;
    } else {
        selector += 5 + (index - 0x20000U) / 0x20000U;
        length = 0x20000U;
    }
    if (erased_until < GAME_BASE || erased_until >= GAME_LIMIT ||
        !erase_sector(selector)) {
        return false;
    }
    erased_until += length;
    return true;
}

static bool program_word(uint32_t address, uint32_t value)
{
    if (value != UINT32_MAX && fmc_word_program(address, value) != FMC_READY) {
        return false;
    }
    return *(const volatile uint32_t *)address == value;
}

static bool write_data(uint32_t count)
{
    for (uint32_t i = 0; i < count; i += 4) {
        uint32_t address = GAME_BASE + cursor;
        if (address == erased_until && !erase_next()) {
            return false;
        }
        fmc_unlock();
        bool ok = program_word(address, get_word(&rx[8 + i]));
        fmc_lock();
        if (!ok) {
            return false;
        }
        cursor += 4;
    }
    return true;
}

/* Replies carry the next offset so a host can detect a lost or stale report. */
static uint8_t handle_request(void)
{
    if (rx[0] != LOADER_REPORT_ID || rx[2] != 0 || rx[3] != 0) {
        return 1;
    }
    uint32_t argument = get_word(&rx[4]);
#ifdef FIXFINDER_LOADER_RAM_TEST
    if (rx[1] != LOADER_INFO) {
        return 1;
    }
#endif
    switch (rx[1]) {
    case LOADER_INFO:
        put_word(&tx[8], GAME_BASE);
        put_word(&tx[12], GAME_LIMIT - GAME_BASE);
        tx[16] = valid_game;
#ifdef FIXFINDER_RADIO
        const volatile uint32_t *uid = (const uint32_t *)0x1fff7a10U;
        for (unsigned int i = 0; i < 3; ++i) {
            put_word(&tx[20 + 4 * i], uid[i]);
        }
#endif
        return 0;
    case LOADER_BEGIN:
        if (!size_ok(argument)) {
            return 1;
        }
        size = 0;
        if (!erase_sector(1) || manifest->magic != UINT32_MAX) {
            return 2;
        }
        valid_game = false;
        cursor = 0;
        erased_until = GAME_BASE;
        expected_crc = get_word(&rx[8]);
        size = argument;
        return 0;
    case LOADER_WRITE:
        if (size == 0 || argument != cursor || cursor == size) {
            return 1;
        }
        uint32_t remaining = size - cursor;
        if (!write_data(remaining < LOADER_DATA_SIZE ? remaining : LOADER_DATA_SIZE)) {
            size = 0;
            return 2;
        }
        return 0;
    case LOADER_COMMIT:
        if (size == 0 || cursor != size || !vectors_ok(size) ||
            game_crc(size) != expected_crc) {
            return 3;
        }
        fmc_unlock();
        valid_game = program_word(GAME_MANIFEST + 4, size) &&
                     program_word(GAME_MANIFEST + 8, expected_crc) &&
                     program_word(GAME_MANIFEST, MANIFEST_MAGIC);
        fmc_lock();
        size = 0;
        return valid_game ? 0 : 2;
    case LOADER_RUN:
        run_requested = valid_game;
        return valid_game ? 0 : 3;
    default:
        return 1;
    }
}

void loader_usb_arm(void)
{
    pending = false;
    usbd_ep_recev(&custom_hid, CUSTOMHID_OUT_EP, rx, sizeof rx);
}

void loader_usb_receive(uint32_t length)
{
    if (length == sizeof rx) {
        pending = true;
    } else {
        loader_usb_arm();
    }
}

void loader_usb_sent(void)
{
    if (run_requested) {
        run_acknowledged = true;
    } else {
        loader_usb_arm();
    }
}

__attribute__((naked, noreturn)) static void jump_to_game(uint32_t stack, uint32_t entry)
{
    __asm volatile("msr msp, r0\n"
                   "bx r1");
}

static void reboot_to_game(void)
{
    loader_screen_draw(VIEW_START, 100);
#ifdef FIXFINDER_RADIO
    if (!radio_mode)
#endif
    usbd_disconnect(&custom_hid);
    usb_mdelay(150);
    uint32_t cookie = screen_available ? BOOT_GAME_LIT : BOOT_GAME;
    BOOT_COOKIE[0] = cookie;
    BOOT_COOKIE[1] = ~cookie;
    NVIC_SystemReset();
}

int main(void)
{
    extern const uint32_t __gVectors[];
    SCB->VTOR = (uint32_t)__gVectors;
    uint32_t cookie = BOOT_COOKIE[0];
    if (BOOT_COOKIE[1] != ~cookie) {
        cookie = 0;
    }
    BOOT_COOKIE[0] = 0;
    BOOT_COOKIE[1] = 0;

#ifndef FIXFINDER_LOADER_RAM_TEST
    if (cookie == BOOT_GAME || cookie == BOOT_GAME_LIT) {
        if (cookie == BOOT_GAME_LIT) {
            loader_screen_resume_backlight();
        }
        if (installed_game_valid()) {
            const uint32_t *vectors = (const uint32_t *)GAME_BASE;
            SCB->VTOR = GAME_BASE;
            __DSB();
            __ISB();
            jump_to_game(vectors[0], vectors[1]);
        }
    }
#endif
#ifdef FIXFINDER_RADIO
    radio_mode = radio_requested();
    if (radio_mode) {
        usb_timer_init();
        radio_init();
    } else
#endif
    {
        usb_gpio_config();
        usb_rcu_config();
        usb_timer_init();
        usbd_init(&custom_hid, USB_CORE_ENUM_HS, &custom_hid_desc, &usbd_custom_hid_cb);
        usb_intr_config();
    }
    __enable_irq();

    screen_available = loader_screen_init();
#ifndef FIXFINDER_LOADER_RAM_TEST
    valid_game = installed_game_valid();
#endif
    bool keep_loader = cookie == BOOT_LOADER;
#ifdef FIXFINDER_RADIO
    keep_loader |= radio_mode;
#endif
    bool countdown = valid_game && !keep_loader;
#ifdef FIXFINDER_RADIO
    if (radio_mode) {
        loader_screen_draw(VIEW_WIFI, 0);
    } else
#endif
    loader_screen_draw(countdown ? VIEW_BOOT : VIEW_WAIT, 0);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t last_request = DWT->CYCCNT;
    const uint32_t boot_window = 5U * SystemCoreClock;
    unsigned int last_progress = 0;
#ifdef FIXFINDER_RADIO
    uint32_t last_wake = last_request;
    if (radio_mode) {
        radio_wake();
    }
#endif
    while (true) {
#ifdef FIXFINDER_RADIO
        if (radio_mode) {
            uint32_t now = DWT->CYCCNT;
            pending = radio_poll(rx, now);
            if (!pending && now - last_wake >= SystemCoreClock &&
                now - last_request >= SystemCoreClock) {
                radio_wake();
                last_wake = now;
            }
        }
#endif
        if (pending) {
            uint8_t command = rx[1];
            if (command == LOADER_COMMIT && size != 0 && cursor == size) {
                loader_screen_draw(VIEW_VERIFY, 100);
            }
            __disable_irq();
            pending = false;
            memset(tx, 0, sizeof tx);
            tx[0] = LOADER_REPORT_ID;
            tx[1] = command;
            uint8_t status = handle_request();
            tx[2] = status;
            tx[3] = LOADER_PROTOCOL_VERSION;
            put_word(&tx[4], cursor);
#ifdef FIXFINDER_RADIO
            if (radio_mode) {
                radio_send(tx);
                run_acknowledged = run_requested;
            } else
#endif
            custom_hid_report_send(&custom_hid, tx, sizeof tx);
            __enable_irq();
            last_request = DWT->CYCCNT;
            if (status == 0) {
                if (command == LOADER_BEGIN) {
                    countdown = false;
                    last_progress = 0;
                    loader_screen_draw(VIEW_INSTALL, 0);
                } else if (command == LOADER_WRITE) {
                    unsigned int progress = (cursor * 20U / size) * 5U;
                    if (progress != last_progress) {
                        loader_screen_draw(VIEW_INSTALL, progress);
                        last_progress = progress;
                    }
                } else if (command == LOADER_COMMIT) {
                    countdown = false;
                    loader_screen_draw(VIEW_READY, 100);
                }
            } else if (status == 2 || (status == 3 && command == LOADER_COMMIT)) {
                countdown = false;
                loader_screen_draw(VIEW_ERROR, 0);
            }
        }
        uint32_t elapsed = DWT->CYCCNT - last_request;
        if (run_acknowledged || (!keep_loader && valid_game && elapsed >= boot_window)) {
            reboot_to_game();
        }
        if (countdown && size == 0 && valid_game) {
            unsigned int progress = elapsed / (boot_window / 20U) * 5U;
            if (progress != last_progress) {
                loader_screen_draw(VIEW_BOOT, progress);
                last_progress = progress;
            }
        }
    }
}
