#include "arcade.h"
#ifdef FIXFINDER_EMMC_LIBRARY
#include "game_library.h"
#endif
#ifdef FIXFINDER_EXTERNAL_DOOM_MAPS
#include "episode_maps.h"
#endif
#include "display.h"
#include "dino.h"
#include "racer.h"
#include "usb_video.h"
#include "../lcd/fixfinder_io.h"
#include "../usb-loader/screen_art.h"
#ifndef FIXFINDER_ARCADE_ONLY
#include "../usb-loader/doom_logo.h"
#endif
#include <stdlib.h>
#include <string.h>

enum color { BG, WHITE, MUTED, ORANGE, GREEN, RED, BLUE, YELLOW, PINK, PANEL,
             EDGE, INSET, BRAND = 16, DOOM_ART = 32, TEXT_PALETTE = 48 };
enum game_id {
#ifndef FIXFINDER_ARCADE_ONLY
    DOOM,
#endif
    RACER, DINO, GAME_COUNT
};
enum { W = DOOM_FRAME_WIDTH, H = DOOM_FRAME_HEIGHT };

static const uint8_t base_palette[768] = {
    [BG*3] = 15, 17, 23, [WHITE*3] = 248, 243, 232,
    [MUTED*3] = 148, 160, 177, [ORANGE*3] = 255, 112, 40,
    [GREEN*3] = 78, 222, 109, [RED*3] = 241, 64, 75,
    [BLUE*3] = 69, 137, 241, [YELLOW*3] = 255, 216, 68,
    [PINK*3] = 235, 115, 184, [PANEL*3] = 29, 34, 46,
    [EDGE*3] = 62, 70, 85, [INSET*3] = 20, 23, 32,
};
static uint8_t palette[sizeof base_palette] __attribute__((section(".arcade_visual")));

struct ui_glyph {
    uint16_t offset;
    int8_t left, top;
    uint8_t width, height, advance;
};
struct ui_font {
    const struct ui_glyph *glyphs;
    const uint8_t *pixels;
};
#include "arcade_font.h"

struct ui_label {
    const struct ui_font *font;
    int x, y;
    char text[40];
};
static struct ui_label labels[8] __attribute__((section(".arcade_visual")));
static unsigned int label_count;
static bool branding;
static uint8_t wordmark[LOGO_WIDTH*LOGO_HEIGHT/2] __attribute__((section(".arcade_visual")));

static const struct {
    const char *name;
    const char *genre;
    const char *hint;
    uint8_t color;
} games[] = {
#if defined(FIXFINDER_EXTERNAL_DOOM_MAPS)
    [DOOM] = {"DOOM", "ACTION", "KNEE-DEEP IN THE DEAD", RED},
#elif !defined(FIXFINDER_ARCADE_ONLY)
    [DOOM] = {"DOOM", "ACTION", "E1M1 HANGAR", RED},
#endif
    [RACER] = {"NEON CIRCUIT", "RACING", "STEER  BOOST  BRAKE", ORANGE},
    [DINO] = {"DINO RUN", "RUNNER", "TAP A BUTTON TO JUMP", GREEN},
};

static void box(uint8_t *frame, int x, int y, int width, int height, uint8_t color)
{
    if (x < 0 || y < 0 || x + width > W || y + height > H) return;
    for (int row = 0; row < height; ++row) {
        memset(frame + (y + row) * W + x, color, width);
    }
}

static const struct ui_glyph *glyph_for(const struct ui_font *font, unsigned char c)
{
    return &font->glyphs[(c >= 32 && c < 127 ? c : '?') - 32];
}

static int text_width(const struct ui_font *font, const char *s)
{
    int width = 0;
    for (; *s; ++s) width += glyph_for(font, *s)->advance;
    return width;
}

static void reset_labels(bool show_brand)
{
    label_count = 0;
    branding = show_brand;
}

/* Coordinates follow the artwork layout. Glyphs are composed at LCD resolution. */
static void text(int x, int y, const char *s, const struct ui_font *font,
                 uint8_t ink, uint8_t paper, bool center)
{
    if (label_count >= sizeof labels / sizeof *labels) return;
    struct ui_label *label = &labels[label_count];
    unsigned int length = strlen(s);
    if (length >= sizeof label->text) return;
    memcpy(label->text, s, length + 1);
    label->x = x * DOOM_LCD_WIDTH / W;
    label->y = y * DOOM_LCD_HEIGHT / H;
    if (center) label->x -= text_width(font, s) / 2;
    label->font = font;
    for (unsigned int alpha = 1; alpha <= 15; ++alpha) {
        unsigned int index = TEXT_PALETTE + label_count * 15 + alpha - 1;
        for (unsigned int component = 0; component < 3; ++component) {
            unsigned int fg = base_palette[ink * 3 + component];
            unsigned int bg = base_palette[paper * 3 + component];
            palette[index * 3 + component] = (fg * alpha + bg * (15 - alpha) + 7) / 15;
        }
    }
    ++label_count;
}

static void number(int x, int y, unsigned int value)
{
    char digits[4] = {
        '0' + value/100%10, '0' + value/10%10, '0' + value%10, 0
    };
    text(x, y, digits, &ui_small, WHITE, BG, false);
}

static void native_row(const uint8_t *frame, unsigned int y, uint8_t *row)
{
    const uint8_t *src = frame + (y * H / DOOM_LCD_HEIGHT) * W;
    for (unsigned int x = 0; x < DOOM_LCD_WIDTH; ++x)
        row[x] = src[x * W / DOOM_LCD_WIDTH];

    enum { logo_top = 7, logo_left = (DOOM_LCD_WIDTH - LOGO_WIDTH) / 2 };
    if (branding && y >= logo_top && y < logo_top + LOGO_HEIGHT) {
        unsigned int start = (y - logo_top) * LOGO_WIDTH;
        for (unsigned int x = 0; x < LOGO_WIDTH; ++x) {
            unsigned int index = (wordmark[(start + x) / 2] >> ((x & 1) * 4)) & 15;
            if (index) row[logo_left + x] = BRAND + index;
        }
    }

    for (unsigned int n = 0; n < label_count; ++n) {
        const struct ui_label *label = &labels[n];
        int dy = (int)y - label->y;
        if (dy < 0 || dy > 34) continue;
        int x = label->x;
        for (const char *s = label->text; *s; ++s) {
            const struct ui_glyph *g = glyph_for(label->font, *s);
            int gy = dy - g->top;
            if (gy >= 0 && gy < g->height) {
                for (unsigned int col = 0; col < g->width; ++col) {
                    int dest = x + g->left + (int)col;
                    if (dest < 0 || dest >= DOOM_LCD_WIDTH) continue;
                    unsigned int pixel = gy * g->width + col;
                    uint8_t packed = label->font->pixels[g->offset + pixel / 2];
                    unsigned int alpha = (packed >> ((pixel & 1) * 4)) & 15;
                    if (alpha) row[dest] = TEXT_PALETTE + n * 15 + alpha - 1;
                }
            }
            x += g->advance;
        }
    }
}

static const uint8_t *usb_native_row(const void *frame, unsigned int y)
{
    static uint8_t row[DOOM_LCD_WIDTH] __attribute__((section(".arcade_visual")));
    native_row(frame, y, row);
    return row;
}

static const uint8_t *preview_row(const void *frame, unsigned int y)
{
    static uint8_t preview[W] __attribute__((section(".arcade_visual")));
    const uint8_t *row = usb_native_row(frame, y * DOOM_LCD_HEIGHT / H);
    for (unsigned int x = 0; x < W; ++x)
        preview[x] = row[x * DOOM_LCD_WIDTH / W];
    return preview;
}

static uint8_t *lcd_shadow;

static void release_lcd_shadow(void)
{
    free(lcd_shadow);
    lcd_shadow = NULL;
}

static void present(uint8_t *frame)
{
    uint8_t row[DOOM_LCD_WIDTH];
    enum { PIXELS = DOOM_LCD_WIDTH * DOOM_LCD_HEIGHT };
    bool dirty = lcd_shadow && memcmp(lcd_shadow + PIXELS, palette, sizeof palette) == 0;
    if (!lcd_shadow) lcd_shadow = malloc(PIXELS + sizeof palette);
    if (!dirty) {
        doom_display_set_palette(palette);
        doom_dcs_begin_native();
        if (lcd_shadow) memcpy(lcd_shadow + PIXELS, palette, sizeof palette);
    }
    for (unsigned int y = 0; y < DOOM_LCD_HEIGHT; ++y) {
        native_row(frame, y, row);
        if (dirty) {
            doom_dcs_update_native_row(y, row, lcd_shadow + y * DOOM_LCD_WIDTH, 0, DOOM_LCD_WIDTH);
        } else {
            doom_dcs_write_native(y, row);
            if (lcd_shadow) memcpy(lcd_shadow + y * DOOM_LCD_WIDTH, row, sizeof row);
        }
    }
    doom_usb_video_capture_native_rows(usb_native_row, preview_row, frame, palette);
}

static void wait_until(const volatile uint32_t *clock, uint32_t deadline)
{
    while ((int32_t)(*clock - deadline) < 0) {}
}

bool fixfinder_arcade_exit(uint32_t keys, uint32_t now)
{
    static uint32_t started;
    static bool holding;
    if (keys != (KEY_DOWN | KEY_BACK)) {
        holding = false;
    } else if (!holding) {
        holding = true;
        started = now;
    }
    return holding && now - started >= 1000U;
}

static void hud(uint8_t *frame, enum game_id game, unsigned int score)
{
    memset(frame, BG, W*H);
    reset_labels(false);
    text(8, 2, games[game].name, &ui_small, games[game].color, BG, false);
    number(211, 2, score);
    text(8, 146, "Hold Down + Back for menu", &ui_small, MUTED, BG, false);
}

static bool finish(uint8_t *frame, const volatile uint32_t *clock, bool won)
{
    fixfinder_set_led(won ? FIXFINDER_LED_GREEN : FIXFINDER_LED_RED);
    box(frame, 40, 55, 160, 49, PANEL);
    text(120, 59, won ? "You win" : "Game over", &ui_large, WHITE, PANEL, true);
    text(120, 86, "Enter to play again", &ui_small, MUTED, PANEL, true);
    bool released = false;
    for (;;) {
        uint32_t now = *clock;
        uint32_t keys = fixfinder_keys(now);
        if (fixfinder_arcade_exit(keys, now)) return false;
        if (!keys) released = true;
        if (released && keys == KEY_ENTER) return true;
        present(frame);
        wait_until(clock, now + 35);
    }
}

static int clamp(int value, int lo, int hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

static void import_colors(unsigned int start, const uint16_t *colors, unsigned int count)
{
    for (unsigned int i=0; i<count; ++i) {
        unsigned int c=colors[i];
        palette[(start+i)*3] = (c>>11)*255/31;
        palette[(start+i)*3+1] = ((c>>5)&63)*255/63;
        palette[(start+i)*3+2] = (c&31)*255/31;
    }
}

static void unpack_art(uint8_t *dst, unsigned int size, const uint8_t *src)
{
    unsigned int at=0;
    while (at<size) {
        unsigned int token=*src++;
        if (token&0x80) {
            unsigned int count=(token&63)+3, distance=*src++;
            if (token&0x40) distance|=(unsigned int)*src++<<8;
            while (count--) { dst[at]=dst[at-distance]; ++at; }
        } else {
            for (unsigned int count=token+1; count; --count) dst[at++]=*src++;
        }
    }
}

#ifndef FIXFINDER_ARCADE_ONLY
static uint8_t doom_art[DOOM_LOGO_WIDTH*DOOM_LOGO_HEIGHT/2] __attribute__((section(".arcade_visual")));

static void art(uint8_t *frame, int x, int y, int w, int h,
                int src_w, int src_h, const uint8_t *packed, uint8_t first,
                uint8_t transparent, uint8_t paper)
{
    for (int row=0; row<h; ++row) for (int col=0; col<w; ++col) {
        int i=(row*src_h/h)*src_w+col*src_w/w;
        unsigned int index=(packed[i/2]>>((i&1)*4))&15;
        frame[(y+row)*W+x+col]=index>=transparent?paper:first+index;
    }
}
#endif

static void title(int y, const char *name)
{
    const struct ui_font *font = text_width(&ui_large, name) <= DOOM_LCD_WIDTH - 24
        ? &ui_large : &ui_small;
    text(120, y, name, font, WHITE, PANEL, true);
}

static void header(uint8_t *frame)
{
    memset(frame,BG,W*H);
    reset_labels(true);
    box(frame,10,26,220,1,EDGE);
    box(frame,10,26,30,1,ORANGE);
}

static void illustration(uint8_t *frame, int x, int y, enum game_id game)
{
    /* 80 by 49 artwork, shared by the selector and the game title cards. */
    for (int col=1; col<80; col+=6)
        for (int row=1; row<49; row+=6) box(frame,x+col,y+row,1,1,EDGE);
    switch (game) {
#ifndef FIXFINDER_ARCADE_ONLY
    case DOOM:
        art(frame,x+2,y+1,76,47,DOOM_LOGO_WIDTH,DOOM_LOGO_HEIGHT,doom_art,DOOM_ART,13,INSET);
        break;
#endif
    case RACER:
        for (int row=-10;row<=10;++row) {
            int half=10-abs(row)/2;
            box(frame,x+53-half,y+16+row,2*half+1,1,row<2?ORANGE:PINK);
        }
        for (int row=0;row<23;++row) {
            int half=6+row;
            box(frame,x+39-half,y+23+row,2*half+1,1,EDGE);
            box(frame,x+39-half,y+23+row,2,1,PINK);
            box(frame,x+39+half-1,y+23+row,2,1,PINK);
        }
        box(frame,x+32,y+34,15,7,BLUE);
        box(frame,x+35,y+32,9,3,WHITE);
        box(frame,x+33,y+39,3,2,RED);
        box(frame,x+43,y+39,3,2,RED);
        break;
    case DINO:
        box(frame,x+8,y+38,64,2,ORANGE);
        box(frame,x+20,y+24,19,11,GREEN);
        box(frame,x+30,y+16,17,11,GREEN);
        box(frame,x+39,y+18,2,2,BG);
        box(frame,x+14,y+24,8,5,GREEN);
        box(frame,x+24,y+33,4,5,GREEN);
        box(frame,x+33,y+33,4,5,GREEN);
        box(frame,x+57,y+25,4,13,YELLOW);
        box(frame,x+53,y+29,11,3,YELLOW);
        break;
    default: break;
    }
}

static void splash(uint8_t *frame, const volatile uint32_t *clock, enum game_id game)
{
    enum { LOAD_MS=900 };
    uint32_t start=*clock;
    do {
        uint32_t now=*clock;
        header(frame);
        box(frame,20,34,200,100,PANEL);
        box(frame,20,34,200,2,games[game].color);
#ifndef FIXFINDER_ARCADE_ONLY
        if (game==DOOM) {
            art(frame,77,40,86,53,DOOM_LOGO_WIDTH,DOOM_LOGO_HEIGHT,doom_art,DOOM_ART,13,PANEL);
            text(120, 99, "Loading Doom", &ui_small, WHITE, PANEL, true);
        } else
#endif
        {
            box(frame,80,39,80,49,INSET);
            illustration(frame,80,39,game);
            title(88,games[game].name);
        }
        text(120, 114, games[game].hint, &ui_small, MUTED, PANEL, true);
        box(frame,20,144,200,4,PANEL);
        box(frame,20,144,clamp((now-start)*200/LOAD_MS,1,200),4,games[game].color);
        present(frame);
        wait_until(clock,now+32);
    } while (*clock-start<LOAD_MS);
}

static void selector(uint8_t *frame, unsigned int selected)
{
    memset(frame,BG,W*H);
    reset_labels(true);
    uint8_t accent = games[selected].color;
    box(frame,0,26,W,96,PANEL);
    box(frame,0,26,W,1,EDGE);
    box(frame,0,121,W,1,EDGE);
    box(frame,35,37,1,73,EDGE);
    box(frame,204,37,1,73,EDGE);
    for (int i=0;i<6;++i) {
        box(frame,15+i,74-i,2,2,MUTED);
        box(frame,15+i,74+i,2,2,MUTED);
        box(frame,223-i,74-i,2,2,MUTED);
        box(frame,223-i,74+i,2,2,MUTED);
    }
#ifndef FIXFINDER_ARCADE_ONLY
    if (selected==DOOM) {
        art(frame,74,30,92,56,DOOM_LOGO_WIDTH,DOOM_LOGO_HEIGHT,doom_art,DOOM_ART,13,PANEL);
    } else
#endif
    {
        illustration(frame, 80, 34, selected);
    }
    title(86, games[selected].name);
    text(120, 105, games[selected].genre, &ui_small, accent, PANEL, true);
    for (unsigned int i = 0; i < GAME_COUNT; ++i)
        box(frame, 120 - (GAME_COUNT * 10 - 4) / 2 + i * 10, 129, 6, 3, i == selected ? accent : EDGE);
    text(12, 142, "Erase  Prev", &ui_small, MUTED, BG, false);
    text(86, 142, "Down  Next", &ui_small, MUTED, BG, false);
    box(frame,166,140,62,17,accent);
    text(197, 142, "Enter  Play", &ui_small, BG, accent, true);
}

void fixfinder_arcade_run(uint8_t *frame, const volatile uint32_t *clock)
{
    unsigned int selected=0;
#ifdef FIXFINDER_EMMC_LIBRARY
    fixfinder_library_mount();
#endif
    uint32_t previous=KEY_BACK|KEY_DOWN|KEY_ENTER|KEY_ERASE;
    memcpy(palette,base_palette,sizeof palette);
    import_colors(BRAND,logo_palette,sizeof logo_palette/sizeof *logo_palette);
    unpack_art(wordmark,sizeof wordmark,logo_data);
#ifndef FIXFINDER_ARCADE_ONLY
    import_colors(DOOM_ART,doom_logo_palette,sizeof doom_logo_palette/sizeof *doom_logo_palette);
    unpack_art(doom_art,sizeof doom_art,doom_logo_data);
#endif
    doom_display_set_palette(palette);
    bool redraw = true;
    for (;;) {
        uint32_t now=*clock, keys=fixfinder_keys(now);
        if (keys!=previous) {
            if (keys==KEY_ERASE) {
                selected=(selected+GAME_COUNT-1)%GAME_COUNT;
                redraw = true;
            }
            if (keys==KEY_DOWN) {
                selected=(selected+1)%GAME_COUNT;
                redraw = true;
            }
            if (keys==KEY_ENTER && previous==0) {
                fixfinder_set_led(FIXFINDER_LED_YELLOW);
                splash(frame,clock,selected);
                release_lcd_shadow();
                switch (selected) {
#ifndef FIXFINDER_ARCADE_ONLY
                case DOOM:
#if defined(FIXFINDER_EXTERNAL_DOOM_MAPS) || defined(FIXFINDER_PROVISION_DOOM_MAPS)
#ifdef FIXFINDER_EXTERNAL_DOOM_MAPS
                    if (doom_maps_available()) return;
                    const char *message = "DOOM map pack missing";
#else
                    const char *message = "Install DOOM episode firmware";
#endif
                    fixfinder_set_led(FIXFINDER_LED_RED);
                    text(120, 125, message, &ui_small, RED, PANEL, true);
                    present(frame);
                    wait_until(clock,*clock+1500);
                    break;
#else
                    return;
#endif
#endif
                case DINO: {
                    unsigned int score;
                    while (fixfinder_dino_run(clock,&score)) {
                        hud(frame,DINO,score);
                        if (!finish(frame,clock,false)) break;
                        release_lcd_shadow();
                    }
                    break;
                }
                case RACER: {
                    enum race_result result;
                    do {
                        result=fixfinder_race(clock);
                        if (result==RACE_EXIT) break;
                        hud(frame,RACER,0);
                        if (!finish(frame,clock,result==RACE_FINISHED)) break;
                        release_lcd_shadow();
                    } while (true);
                    break;
                }
                }
                keys=fixfinder_keys(*clock);
                redraw = true;
            }
            previous=keys;
        }
        if (redraw) {
            fixfinder_set_led(FIXFINDER_LED_OFF);
            selector(frame,selected);
            present(frame);
            redraw = false;
        } else {
            doom_usb_video_capture_native_rows(usb_native_row, preview_row, frame, palette);
        }
        wait_until(clock,now+1);
    }
}
