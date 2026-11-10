#include "arcade.h"
#include "save_files.h"
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
#ifdef FIXFINDER_USB_DESKTOP
#include "usb_desktop.h"
#endif
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
    SNAKE, MAZE, PONG, BREAKOUT, RACER, DINO, GAME_COUNT
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
    [SNAKE] = {"SNAKE", "SURVIVAL", "FOUR KEYS STEER", GREEN},
#ifdef FIXFINDER_PACMAN
    [MAZE] = {"PAC MAN", "MIDWAY 1980", "ERASE AND ENTER TO START", YELLOW},
#else
    [MAZE] = {"MAZE CHASE", "MAZE", "EAT DOTS  DODGE GHOSTS", YELLOW},
#endif
    [PONG] = {"PONG", "SPORT", "ERASE UP  DOWN DOWN", BLUE},
    [BREAKOUT] = {"BREAKOUT", "PADDLE", "BACK LEFT  ENTER RIGHT", PINK},
    [RACER] = {"NEON CIRCUIT", "RACING", "STEER  BOOST  BRAKE", ORANGE},
    [DINO] = {"DINO RUN", "RUNNER", "TAP A BUTTON TO JUMP", GREEN},
};

static const int dx[] = {1, 0, -1, 0};
static const int dy[] = {0, 1, 0, -1};

enum arcade_sound_priority { BOUNCE = 1, ACTION, RESULT };
static const struct fixfinder_note win_sound[] = {{660, 50}, {880, 55}, {1320, 90}, {0, 0}};
static const struct fixfinder_note lose_sound[] = {{495, 60}, {330, 70}, {220, 105}, {0, 0}};
static const struct fixfinder_note snake_food[] = {{650, 20}, {980, 34}, {0, 0}};
#ifndef FIXFINDER_PACMAN
static const struct fixfinder_note maze_dot[] = {{790, 13}, {530, 15}, {0, 0}};
static const struct fixfinder_note maze_power[] = {{450, 25}, {670, 32}, {1010, 62}, {0, 0}};
static const struct fixfinder_note maze_ghost[] = {{1160, 28}, {760, 28}, {1390, 42}, {0, 0}};
#endif
static const struct fixfinder_note wall_sound[] = {{310, 16}, {0, 0}};
static const struct fixfinder_note paddle_sound[] = {{555, 25}, {0, 0}};
static const struct fixfinder_note brick_sound[] = {{820, 18}, {1050, 24}, {0, 0}};
static const struct fixfinder_note point_sound[] = {{645, 36}, {940, 54}, {0, 0}};
static const struct fixfinder_note miss_sound[] = {{290, 32}, {185, 68}, {0, 0}};

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

static int direction(uint32_t keys, int fallback)
{
    switch (keys) {
    case KEY_ENTER: return 0;
    case KEY_DOWN: return 1;
    case KEY_BACK: return 2;
    case KEY_ERASE: return 3;
    default: return fallback;
    }
}

static void hud(uint8_t *frame, enum game_id game, unsigned int score)
{
    memset(frame, BG, W*H);
    reset_labels(false);
    text(8, 2, games[game].name, &ui_small, games[game].color, BG, false);
    number(211, 2, score);
#ifdef FIXFINDER_SAVE_FILES
    text(8, 146, "Down + Back to save", &ui_small, MUTED, BG, false);
#else
    text(8, 146, "Hold Down + Back for menu", &ui_small, MUTED, BG, false);
#endif
}

static bool finish(uint8_t *frame, const volatile uint32_t *clock, bool won)
{
    fixfinder_play_sound(won ? win_sound : lose_sound, RESULT);
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
        if (released && keys == KEY_ENTER) {
            fixfinder_stop_sound();
            return true;
        }
        present(frame);
        wait_until(clock, now + 35);
    }
}

static unsigned int random_cell(unsigned int *seed, unsigned int cells)
{
    *seed = *seed * 1664525U + 1013904223U;
    return (*seed >> 16) % cells;
}

static void snake(uint8_t *frame, const volatile uint32_t *clock, bool resume)
{
    enum { C = 28, R = 15, CELLS = C*R, TILE = 8 };
    bool won;
    do {
        struct {
            uint16_t body[CELLS];
            unsigned int seed, length, food;
            int heading, next;
        } state = {{7*C+7, 7*C+6, 7*C+5}, *clock, 3, 7*C+16, 0, 0};
        if (resume) fixfinder_save_read("SNAKE", &state, sizeof state);
        resume = false;
        uint32_t step = *clock + 190, food_flash = *clock;
        bool redraw = true;
        won = false;
        for (;;) {
            uint32_t now = *clock, keys = fixfinder_keys(now);
            if (fixfinder_arcade_suspend(keys, now)) {
                fixfinder_save_write("SNAKE", &state, sizeof state);
                return;
            }
            int turn = direction(keys, state.next);
            if (((turn + 2) & 3) != state.heading) state.next = turn;
            if ((int32_t)(now - step) >= 0) {
                state.heading = state.next;
                int x = state.body[0]%C + dx[state.heading], y = state.body[0]/C + dy[state.heading];
                if (x < 0 || x >= C || y < 0 || y >= R) break;
                unsigned int cell = y*C+x;
                bool grow = cell == state.food;
                unsigned int retained = state.length - !grow;
                bool hit = false;
                for (unsigned int i = 0; i < retained; ++i) hit |= state.body[i] == cell;
                if (hit) break;
                memmove(state.body+1, state.body, retained*sizeof *state.body);
                state.body[0] = cell;
                if (grow && ++state.length == CELLS) { won = true; break; }
                if (grow) {
                    fixfinder_play_sound(snake_food, ACTION);
                    food_flash = now + 220;
                    do {
                        state.food = random_cell(&state.seed, CELLS);
                        hit = false;
                        for (unsigned int i = 0; i < state.length; ++i) hit |= state.body[i] == state.food;
                    } while (hit);
                }
                step = now + (state.length < 22 ? 190 - state.length*5 : 80);
                redraw = true;
            }
            fixfinder_set_led((int32_t)(food_flash - now) > 0 ? FIXFINDER_LED_YELLOW : FIXFINDER_LED_GREEN);
            if (!redraw) {
                doom_usb_video_capture_native_rows(usb_native_row, preview_row, frame, palette);
                wait_until(clock,now+4);
                continue;
            }
            hud(frame, SNAKE, state.length - 3);
            box(frame, 7, 19, 226, 122, PANEL);
            box(frame, 9+(state.food%C)*TILE, 21+(state.food/C)*TILE, 5, 5, RED);
            for (unsigned int i = 0; i < state.length; ++i)
                box(frame, 8+(state.body[i]%C)*TILE, 20+(state.body[i]/C)*TILE, 7, 7, i ? GREEN : WHITE);
            present(frame);
            redraw = false;
        }
    } while (finish(frame, clock, won));
}

#ifdef FIXFINDER_PACMAN
bool fixfinder_pacman_run(uint8_t *frame, const volatile uint32_t *clock, bool resume);
#else
/* A compact original maze. Spaces are traversable and dots are collectible. */
static const char maze_map[][22] = {
    "#####################",
    "#o........#........o#",
    "#.###.###.#.###.###.#",
    "#...................#",
    "#.###.#.#####.#.###.#",
    "#.....#...#...#.....#",
    "###.#.###.#.###.#.###",
    "#...#...........#...#",
    "#.#.#.#.#####.#.#.#.#",
    "#.#...#... ...#...#.#",
    "#.###.#.#####.#.###.#",
    "#...................#",
    "#.###.###.#.###.###.#",
    "#o........#........o#",
    "#####################",
};

static bool maze_open(int x, int y)
{
    return x >= 0 && x < 21 && y >= 0 && y < 15 && maze_map[y][x] != '#';
}

static void maze(uint8_t *frame, const volatile uint32_t *clock, bool resume)
{
    do {
        struct {
            char dots[15][22];
            unsigned int left, score, turns, power_ms, safe_ms;
            int x, y, heading, next, lives, gx[2], gy[2], gd[2];
        } state = {.safe_ms=900,.x=10,.y=11,.heading=2,.next=2,.lives=3,
                   .gx={1,19},.gy={7,7},.gd={0,2}};
        memcpy(state.dots, maze_map, sizeof state.dots);
        for (int y = 0; y < 15; ++y)
            for (int x = 0; x < 21; ++x)
                state.left += state.dots[y][x] == '.' || state.dots[y][x] == 'o';
        if (resume) fixfinder_save_read("MAZE", &state, sizeof state);
        resume = false;
        uint32_t step = *clock + 130, power_until = *clock + state.power_ms;
        uint32_t safe_until = *clock + state.safe_ms;
        while (state.left && state.lives) {
            uint32_t now = *clock, keys = fixfinder_keys(now);
            if (fixfinder_arcade_suspend(keys, now)) {
                state.power_ms = (int32_t)(power_until-now) > 0 ? power_until-now : 0;
                state.safe_ms = (int32_t)(safe_until-now) > 0 ? safe_until-now : 0;
                fixfinder_save_write("MAZE", &state, sizeof state);
                return;
            }
            state.next = direction(keys, state.next);
            bool powered = (int32_t)(power_until - now) > 0;
            if ((int32_t)(now-step) >= 0) {
                if (maze_open(state.x+dx[state.next], state.y+dy[state.next])) state.heading = state.next;
                if (maze_open(state.x+dx[state.heading], state.y+dy[state.heading])) { state.x+=dx[state.heading]; state.y+=dy[state.heading]; }
                if (state.dots[state.y][state.x] == '.' || state.dots[state.y][state.x] == 'o') {
                    if (state.dots[state.y][state.x] == 'o') {
                        power_until = now + 5500;
                        powered = true;
                        fixfinder_play_sound(maze_power, ACTION);
                    } else {
                        fixfinder_play_sound(maze_dot, BOUNCE);
                    }
                    state.dots[state.y][state.x] = ' ';
                    --state.left; ++state.score;
                }
                if (++state.turns % 2 == 0) for (int g = 0; g < 2; ++g) {
                    int best = 1000, move = -1;
                    for (int d = 0; d < 4; ++d) {
                        int nx = state.gx[g]+dx[d], ny = state.gy[g]+dy[d];
                        if (!maze_open(nx, ny)) continue;
                        int ax = nx-state.x, ay = ny-state.y;
                        int dist = (ax<0?-ax:ax)+(ay<0?-ay:ay);
                        int rank = (powered ? -dist : dist) + (d == ((state.gd[g]+2)&3) ? 80 : 0);
                        if (rank < best) { best = rank; move = d; }
                    }
                    if (move >= 0) { state.gd[g]=move; state.gx[g]+=dx[move]; state.gy[g]+=dy[move]; }
                }
                step = now+125;
            }
            for (int g=0; g<2; ++g) if (state.gx[g]==state.x && state.gy[g]==state.y) {
                if (powered) {
                    state.gx[g]=g?19:1; state.gy[g]=7; state.score+=10;
                    fixfinder_play_sound(maze_ghost, ACTION);
                }
                else if ((int32_t)(now-safe_until)>=0) {
                    --state.lives; state.x=10; state.y=11; safe_until=now+1400;
                    if (state.lives) fixfinder_play_sound(miss_sound, RESULT);
                }
            }
            fixfinder_set_led(state.lives <= 1 ? FIXFINDER_LED_RED :
                              state.lives == 2 || powered ? FIXFINDER_LED_YELLOW : FIXFINDER_LED_GREEN);
            hud(frame, MAZE, state.score);
            for (int row=0; row<15; ++row) for (int col=0; col<21; ++col) {
                int px=36+col*8, py=19+row*8;
                if (state.dots[row][col]=='#') box(frame,px,py,8,8,BLUE);
                if (state.dots[row][col]=='.') box(frame,px+3,py+3,2,2,MUTED);
                if (state.dots[row][col]=='o') box(frame,px+2,py+2,4,4,WHITE);
            }
            box(frame,37+state.x*8,20+state.y*8,6,6,YELLOW);
            box(frame,37+state.x*8+(state.heading==0?4:0),22+state.y*8,2,2,BG);
            for (int g=0; g<2; ++g) box(frame,37+state.gx[g]*8,20+state.gy[g]*8,6,6,powered?MUTED:(g?PINK:RED));
            for (int i=0;i<state.lives;++i) box(frame,12,25+10*i,5,5,YELLOW);
            present(frame);
            wait_until(clock,now+32);
        }
        if (!finish(frame, clock, state.left==0)) return;
    } while (true);
}

#endif

static int clamp(int value, int lo, int hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

static void paddle_game(uint8_t *frame, const volatile uint32_t *clock, bool bricks, bool resume)
{
    /* Half-pixel coordinates keep the same movement speed at twice the tick rate. */
    enum { P = 2 };
    const char *save = bricks ? "BREAKOUT" : "PONG";
    do {
        struct {
            int player, opponent, x, y, vx, vy;
            unsigned int score, other;
            uint16_t rows[4];
        } state = { (bricks ? 105 : 65)*P, 65*P, 120*P, 85*P, -3, 2,
                    0, 0, {0x3ff,0x3ff,0x3ff,0x3ff} };
        if (resume) fixfinder_save_read(save, &state, sizeof state);
        resume = false;
        while (state.score < (bricks ? 40U : 5U) && state.other < (bricks ? 3U : 5U)) {
            uint32_t now = *clock, keys = fixfinder_keys(now);
            if (fixfinder_arcade_suspend(keys, now)) {
                fixfinder_save_write(save, &state, sizeof state);
                return;
            }
            if (bricks) {
                if (keys == KEY_BACK) state.player -= 5;
                if (keys == KEY_ENTER) state.player += 5;
                state.player = clamp(state.player, 8*P, 199*P);
            } else {
                if (keys == KEY_ERASE) state.player -= 5;
                if (keys == KEY_DOWN) state.player += 5;
                state.player = clamp(state.player, 20*P, 117*P);
                state.opponent = clamp(state.opponent + (state.y < state.opponent+10*P ? -2 : 2), 20*P, 117*P);
            }
            state.x += state.vx; state.y += state.vy;
            if (state.y <= 20*P) {
                state.y=20*P; state.vy=2;
                fixfinder_play_sound(wall_sound, BOUNCE);
            }
            if (!bricks && state.y >= 137*P) {
                state.y=137*P; state.vy=-2;
                fixfinder_play_sound(wall_sound, BOUNCE);
            }
            if (bricks) {
                if (state.x <= 8*P || state.x >= 228*P) {
                    state.x=clamp(state.x,8*P,228*P); state.vx=-state.vx;
                    fixfinder_play_sound(wall_sound, BOUNCE);
                }
                if (state.vy > 0 && state.y >= 130*P && state.y <= 135*P && state.x+4*P >= state.player && state.x < state.player+32*P) {
                    state.vy=-2;
                    fixfinder_play_sound(paddle_sound, BOUNCE);
                }
                if (state.y >= 141*P) {
                    ++state.other; state.x=120*P; state.y=94*P; state.vy=-2;
                    if (state.other < 3) fixfinder_play_sound(miss_sound, RESULT);
                }
                if (state.y >= 30*P && state.y < 66*P && state.x >= 19*P && state.x < 219*P) {
                    int row=(state.y-30*P)/(9*P), col=(state.x-19*P)/(20*P);
                    if (state.rows[row] & (1U << col)) {
                        state.rows[row] ^= 1U << col; ++state.score; state.vy=-state.vy;
                        fixfinder_play_sound(brick_sound, ACTION);
                    }
                }
            } else {
                if (state.vx<0 && state.x<=17*P && state.x>=11*P && state.y+4*P>=state.player && state.y<state.player+23*P) {
                    state.vx=3;
                    fixfinder_play_sound(paddle_sound, BOUNCE);
                }
                if (state.vx>0 && state.x>=219*P && state.x<=225*P && state.y+4*P>=state.opponent && state.y<state.opponent+23*P) {
                    state.vx=-3;
                    fixfinder_play_sound(paddle_sound, BOUNCE);
                }
                if (state.x<6*P || state.x>230*P) {
                    if (state.x<6*P) ++state.other; else ++state.score;
                    if (state.other < 5 && state.score < 5)
                        fixfinder_play_sound(state.x < 6*P ? miss_sound : point_sound, RESULT);
                    state.vx = state.x<6*P ? 3 : -3;
                    state.x=120*P; state.y=80*P;
                }
            }
            fixfinder_set_led(bricks && state.other >= 2 ? FIXFINDER_LED_RED :
                              state.other > (bricks ? 0 : state.score) ? FIXFINDER_LED_YELLOW : FIXFINDER_LED_GREEN);
            hud(frame,bricks?BREAKOUT:PONG,state.score);
            number(183, 2, bricks ? 3-state.other : state.other);
            box(frame, 7, 18, 226, 2, PANEL);
            box(frame, 7, 141, 226, 2, PANEL);
            if (bricks) {
                for (int row=0; row<4; ++row) for (int col=0; col<10; ++col)
                    if (state.rows[row] & (1U << col)) box(frame,19+col*20,30+row*9,18,7,RED+row);
                box(frame,state.player/P,134,32,4,WHITE);
            } else {
                for (int i=24;i<137;i+=9) box(frame,119,i,2,5,PANEL);
                box(frame,12,state.player/P,4,23,GREEN);
                box(frame,224,state.opponent/P,4,23,ORANGE);
            }
            box(frame,state.x/P,state.y/P,4,4,WHITE);
            present(frame);
            wait_until(clock,now+16);
        }
        if (!finish(frame,clock,state.score==(bricks?40U:5U))) return;
    } while (true);
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
    case SNAKE:
        box(frame,x+10,y+30,34,7,GREEN);
        box(frame,x+36,y+14,8,22,GREEN);
        box(frame,x+43,y+14,15,8,GREEN);
        box(frame,x+52,y+15,2,2,BG);
        box(frame,x+66,y+15,5,5,RED);
        box(frame,x+67,y+13,2,2,WHITE);
        break;
    case MAZE:
        box(frame,x+5,y+5,70,2,BLUE);
        box(frame,x+5,y+41,70,2,BLUE);
        for (int row=0; row<18; ++row) {
            int edge=row<3?3-row:row>14?row-14:0;
            int mouth=5-(row<9?9-row:row-9);
            box(frame,x+11+edge,y+15+row,18-edge*2-(mouth>0?mouth:0),1,YELLOW);
        }
        for (int i=0; i<3; ++i) box(frame,x+33+i*7,y+23,2,2,WHITE);
        box(frame,x+55,y+17,14,15,PINK);
        box(frame,x+58,y+14,8,4,PINK);
        box(frame,x+58,y+20,3,4,WHITE);
        box(frame,x+64,y+20,3,4,WHITE);
        box(frame,x+58,y+20,1,2,BG);
        box(frame,x+64,y+20,1,2,BG);
        break;
    case PONG:
        for (int row=3; row<46; row+=6) box(frame,x+39,y+row,2,3,EDGE);
        box(frame,x+10,y+23,4,17,BLUE);
        box(frame,x+66,y+9,4,17,ORANGE);
        box(frame,x+47,y+17,4,4,WHITE);
        box(frame,x+40,y+22,2,2,MUTED);
        box(frame,x+34,y+27,1,1,MUTED);
        break;
    case BREAKOUT: {
        static const uint8_t brick[]={RED,PINK,ORANGE};
        for (int row=0; row<3; ++row) for (int col=0; col<5; ++col)
            if (row<2 || col!=2) box(frame,x+8+13*col,y+7+7*row,11,5,brick[row]);
        box(frame,x+43,y+29,3,3,WHITE);
        box(frame,x+32,y+38,22,4,BLUE);
        break;
    }
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
    uint8_t accent=games[selected].color;
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
        illustration(frame,80,34,selected);
    }
    title(86,games[selected].name);
    text(120, 105, games[selected].genre, &ui_small, accent, PANEL, true);
    for (unsigned int i=0;i<GAME_COUNT;++i)
        box(frame,120-(GAME_COUNT*10-4)/2+i*10,123,6,3,i==selected?accent:EDGE);
#ifdef FIXFINDER_SAVE_FILES
    if (fixfinder_save_notice) {
        text(120, 127, fixfinder_save_notice, &ui_small, WHITE, BG, true);
    }
#ifndef FIXFINDER_ARCADE_ONLY
    else if (selected != DOOM)
#else
    else
#endif
        text(120, 127, "Back: new run", &ui_small, MUTED, BG, true);
#endif
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
#ifdef FIXFINDER_PACMAN_EMMC
    (void)fixfinder_library_read_pacman(frame, PACMAN_ROM_SIZE);
#endif
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
    bool redraw = true, wait_for_release = false;
    for (;;) {
#ifdef FIXFINDER_USB_DESKTOP
        if (fixfinder_desktop_run(clock)) {
            release_lcd_shadow();
            redraw = wait_for_release = true;
        }
#endif
        uint32_t now=*clock, keys=fixfinder_keys(now);
        if (!keys) wait_for_release = false;
        if (!wait_for_release && keys!=previous) {
            bool resume = keys == KEY_ENTER;
            bool launch = resume;
#ifdef FIXFINDER_SAVE_FILES
            if (keys == KEY_ERASE || keys == KEY_DOWN) fixfinder_save_notice = NULL;
#ifndef FIXFINDER_ARCADE_ONLY
            if (selected != DOOM)
#endif
                launch |= keys == KEY_BACK;
#endif
            if (keys==KEY_ERASE) {
                selected=(selected+GAME_COUNT-1)%GAME_COUNT;
                redraw = true;
            }
            if (keys==KEY_DOWN) {
                selected=(selected+1)%GAME_COUNT;
                redraw = true;
            }
            if (launch && previous==0) {
#ifdef FIXFINDER_SAVE_FILES
                fixfinder_save_notice = NULL;
#endif
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
                case SNAKE: snake(frame,clock,resume); break;
                case MAZE:
#ifdef FIXFINDER_PACMAN
                    if (!fixfinder_pacman_run(frame,clock,resume)) {
                        fixfinder_set_led(FIXFINDER_LED_RED);
                        doom_display_set_palette(palette);
                        text(120, 125, "Could not load Pac-Man ROM", &ui_small, RED, PANEL, true);
                        present(frame);
                        wait_until(clock,*clock+1200);
                    }
                    doom_display_set_palette(palette);
#else
                    maze(frame,clock,resume);
#endif
                    break;
                case PONG: paddle_game(frame,clock,false,resume); break;
                case BREAKOUT: paddle_game(frame,clock,true,resume); break;
                case DINO: {
                    unsigned int score;
                    while (fixfinder_dino_run(clock,&score,resume)) {
                        resume = false;
                        hud(frame,DINO,score);
                        if (!finish(frame,clock,false)) break;
                        release_lcd_shadow();
                    }
                    break;
                }
                case RACER: {
                    enum race_result result;
                    do {
                        result=fixfinder_race(clock,resume);
                        resume = false;
                        if (result==RACE_EXIT) break;
                        hud(frame,RACER,0);
                        if (!finish(frame,clock,result==RACE_FINISHED)) break;
                        release_lcd_shadow();
                    } while (true);
                    break;
                }
                }
                fixfinder_stop_sound();
                keys=fixfinder_keys(*clock);
                wait_for_release = keys != 0;
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
