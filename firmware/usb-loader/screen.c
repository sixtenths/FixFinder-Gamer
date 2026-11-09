#include "screen.h"
#include "../lcd/exmc_dcs.h"
#include "drv_usb_hw.h"

#include "screen_art.h"

#define WIDTH 320
#define HEIGHT 240
#define BG 0x18c3U
#define CARD 0x2924U
#define TRACK 0x4207U
#define INK 0xffdfU
#define ORANGE 0xfc00U
#define RED 0xe947U
#define BAR_Y 215U

static bool available;
static int drawn_view = -1;
static unsigned int drawn_width;

static void axis(uint8_t command, unsigned int first, unsigned int last)
{
    LCD_INDEX = command;
    __DSB();
    write_data((uint8_t)(first >> 8));
    write_data((uint8_t)first);
    write_data((uint8_t)(last >> 8));
    write_data((uint8_t)last);
}

static void window(unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
    axis(0x2a, x, x + w - 1);
    axis(0x2b, y, y + h - 1);
    LCD_INDEX = 0x2c;
    __DSB();
}

static void pixel(uint16_t color)
{
    write_data((uint8_t)(color >> 8));
    write_data((uint8_t)color);
}

static void fill(unsigned int x, unsigned int y, unsigned int w, unsigned int h,
                 uint16_t color)
{
    window(x, y, w, h);
    for (unsigned int i = 0; i < w * h; ++i) {
        pixel(color);
    }
}

static uint16_t blend(uint16_t ink, uint16_t paper, unsigned int alpha)
{
    const unsigned int inv = 3 - alpha;
    unsigned int r = ((ink >> 11) * alpha + (paper >> 11) * inv + 1) / 3;
    unsigned int g = (((ink >> 5) & 63) * alpha + ((paper >> 5) & 63) * inv + 1) / 3;
    unsigned int b = ((ink & 31) * alpha + (paper & 31) * inv + 1) / 3;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void label(unsigned int x, unsigned int y, const char *text, uint16_t paper)
{
    const uint16_t colors[] = {paper, blend(INK, paper, 1), blend(INK, paper, 2), INK};
    for (; *text; ++text) {
        if (*text == ' ') {
            x += 5;
            continue;
        }
        unsigned int letter = (unsigned int)(*text - 'A');
        unsigned int width = large_width[letter];
        if (width == 0) {
            continue;
        }
        const uint8_t *glyph = large_glyphs + large_offset[letter];
        window(x, y, width, LARGE_HEIGHT);
        for (unsigned int i = 0; i < width * LARGE_HEIGHT; ++i) {
            pixel(colors[(glyph[i / 4] >> (2 * (i % 4))) & 3U]);
        }
        x += width + 1;
    }
}

static void draw_logo(unsigned int y, unsigned int width, unsigned int height,
                      const uint8_t *src, const uint16_t *palette, unsigned int scale)
{
    /* The stream uses literal runs and overlapping back-references. */
    const unsigned int size = width * height / 2;
    uint8_t packed[size];
    unsigned int at = 0;
    while (at < size) {
        uint8_t token = *src++;
        if (token & 0x80U) {
            unsigned int count = (token & 0x3fU) + 3;
            unsigned int distance = *src++;
            if (token & 0x40U) {
                distance |= (unsigned int)*src++ << 8;
            }
            while (count--) {
                packed[at] = packed[at - distance];
                ++at;
            }
        } else {
            for (unsigned int count = token + 1U; count != 0; --count) {
                packed[at++] = *src++;
            }
        }
    }
    window((WIDTH - width * scale) / 2, y, width * scale, height * scale);
    for (unsigned int row = 0; row < height; ++row) {
        for (unsigned int repeat = 0; repeat < scale; ++repeat) {
            for (unsigned int x = 0; x < width; ++x) {
                unsigned int i = row * width + x;
                uint16_t color = palette[(packed[i / 2] >> ((i & 1U) * 4)) & 15U];
                for (unsigned int col = 0; col < scale; ++col) {
                    pixel(color);
                }
            }
        }
    }
}

bool loader_screen_init(void)
{
    init_display_bus();
    usb_mdelay(150);
    if (!panel_matches()) {
        return false;
    }
    start_panel(usb_mdelay, 0xa0);
    fill(0, 0, WIDTH, HEIGHT, BG);
    draw_logo((66 - LOGO_HEIGHT) / 2, LOGO_WIDTH, LOGO_HEIGHT, logo_data, logo_palette, 1);
    fill(0, 66, WIDTH, 3, ORANGE);
    available = true;
    loader_screen_draw(VIEW_CHECKING, 0);
    start_backlight();
    return true;
}

void loader_screen_draw(enum loader_view view, unsigned int percent)
{
    static const char *const labels[] = {
        [VIEW_BOOT] = "BOOTING",
        [VIEW_WAIT] = "USB LOADER READY",
        [VIEW_INSTALL] = "INSTALLING GAME",
        [VIEW_VERIFY] = "VERIFYING GAME",
        [VIEW_READY] = "GAME VERIFIED",
        [VIEW_ERROR] = "UPDATE FAILED",
#ifdef FIXFINDER_RADIO
        [VIEW_WIFI] = "WIFI LOADER READY"
#endif
    };
    if (!available) {
        return;
    }
    if (view == VIEW_START) {
        view = VIEW_BOOT;
        percent = 100;
    } else if (view == VIEW_CHECKING) {
        view = VIEW_BOOT;
    }
    if (drawn_view != (int)view) {
        fill(0, 69, WIDTH, HEIGHT - 69, BG);
        fill(22, 115, 276, 115, CARD);
        label(view == VIEW_BOOT ? 117 : 36, 147, labels[view], CARD);
        fill(36, BAR_Y, 248, 12, TRACK);
        drawn_view = view;
        drawn_width = 0;
    }
    unsigned int width = percent * 248 / 100;
    if (width > drawn_width) {
        fill(36 + drawn_width, BAR_Y, width - drawn_width, 12, ORANGE);
    } else if (width < drawn_width) {
        fill(36 + width, BAR_Y, drawn_width - width, 12, TRACK);
    }
    drawn_width = width;
    if (view == VIEW_ERROR) {
        fill(36, BAR_Y, 248, 12, RED);
    }
}

void loader_screen_resume_backlight(void)
{
    RCU_AHB1EN |= RCU_AHB1EN_PFEN;
    (void)RCU_AHB1EN;
    start_backlight();
}
