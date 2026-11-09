#include "arcade.h"
#ifdef FIXFINDER_EMMC_LIBRARY
#include "game_library.h"
#endif
#include "display.h"
#include "racer.h"
#include "usb_video.h"
#include "../lcd/fixfinder_io.h"
#include "../usb-loader/screen_art.h"
#include "../usb-loader/doom_logo.h"
#include <stdlib.h>
#include <string.h>

enum color { BG, WHITE, MUTED, ORANGE, GREEN, RED, BLUE, YELLOW, PINK, PANEL,
             EDGE, INSET, BRAND = 16, DOOM_ART = 32, TEXT_PALETTE = 48 };
enum game_id { DOOM, SNAKE, MAZE, PONG, BREAKOUT, RACER, GAME_COUNT };
enum { W = DOOM_FRAME_WIDTH, H = DOOM_FRAME_HEIGHT };

static const uint8_t base_palette[768] = {
    [BG*3] = 15, 17, 23, [WHITE*3] = 248, 243, 232,
    [MUTED*3] = 148, 160, 177, [ORANGE*3] = 255, 112, 40,
    [GREEN*3] = 78, 222, 109, [RED*3] = 241, 64, 75,
    [BLUE*3] = 69, 137, 241, [YELLOW*3] = 255, 216, 68,
    [PINK*3] = 235, 115, 184, [PANEL*3] = 29, 34, 46,
    [EDGE*3] = 62, 70, 85, [INSET*3] = 20, 23, 32,
};
static uint8_t palette[sizeof base_palette] __attribute__((section(".arcade_art")));

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
static struct ui_label labels[8] __attribute__((section(".arcade_art")));
static unsigned int label_count;
static bool branding;
static uint8_t wordmark[LOGO_WIDTH*LOGO_HEIGHT/2] __attribute__((section(".arcade_art")));

static const struct {
    const char *name;
    const char *genre;
    const char *hint;
    uint8_t color;
} games[] = {
    [DOOM] = {"DOOM", "ACTION", "E1M1 HANGAR", RED},
    [SNAKE] = {"SNAKE", "SURVIVAL", "FOUR KEYS STEER", GREEN},
#ifdef FIXFINDER_PACMAN
    [MAZE] = {"PAC MAN", "MIDWAY 1980", "ERASE AND ENTER TO START", YELLOW},
#else
    [MAZE] = {"MAZE CHASE", "MAZE", "EAT DOTS  DODGE GHOSTS", YELLOW},
#endif
    [PONG] = {"PONG", "SPORT", "ERASE UP  DOWN DOWN", BLUE},
    [BREAKOUT] = {"BREAKOUT", "PADDLE", "BACK LEFT  ENTER RIGHT", PINK},
    [RACER] = {"NEON CIRCUIT", "RACING", "STEER  BOOST  BRAKE", ORANGE},
};

static const int dx[] = {1, 0, -1, 0};
static const int dy[] = {0, 1, 0, -1};

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
    static uint8_t row[DOOM_LCD_WIDTH] __attribute__((section(".arcade_art")));
    native_row(frame, y, row);
    return row;
}

static const uint8_t *preview_row(const void *frame, unsigned int y)
{
    static uint8_t preview[W] __attribute__((section(".arcade_art")));
    const uint8_t *row = usb_native_row(frame, y * DOOM_LCD_HEIGHT / H);
    for (unsigned int x = 0; x < W; ++x)
        preview[x] = row[x * DOOM_LCD_WIDTH / W];
    return preview;
}

static void present(uint8_t *frame)
{
    uint8_t row[DOOM_LCD_WIDTH];
    doom_display_set_palette(palette);
    doom_dcs_begin_native();
    for (unsigned int y = 0; y < DOOM_LCD_HEIGHT; ++y) {
        native_row(frame, y, row);
        doom_dcs_write_native(y, row);
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
    text(8, 146, "Hold Down + Back for menu", &ui_small, MUTED, BG, false);
}

static bool finish(uint8_t *frame, const volatile uint32_t *clock, bool won)
{
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

static unsigned int random_cell(unsigned int *seed, unsigned int cells)
{
    *seed = *seed * 1664525U + 1013904223U;
    return (*seed >> 16) % cells;
}

static void snake(uint8_t *frame, const volatile uint32_t *clock)
{
    enum { C = 28, R = 15, CELLS = C*R, TILE = 8 };
    unsigned int seed = *clock;
    bool won;
    do {
        uint16_t body[CELLS] = {7*C+7, 7*C+6, 7*C+5};
        unsigned int length = 3, food = 7*C+16;
        int heading = 0, next = 0;
        uint32_t step = *clock + 190;
        won = false;
        for (;;) {
            uint32_t now = *clock, keys = fixfinder_keys(now);
            if (fixfinder_arcade_exit(keys, now)) return;
            int turn = direction(keys, next);
            if (((turn + 2) & 3) != heading) next = turn;
            if ((int32_t)(now - step) >= 0) {
                heading = next;
                int x = body[0]%C + dx[heading], y = body[0]/C + dy[heading];
                if (x < 0 || x >= C || y < 0 || y >= R) break;
                unsigned int cell = y*C+x;
                bool grow = cell == food;
                unsigned int retained = length - !grow;
                bool hit = false;
                for (unsigned int i = 0; i < retained; ++i) hit |= body[i] == cell;
                if (hit) break;
                memmove(body+1, body, retained*sizeof *body);
                body[0] = cell;
                if (grow && ++length == CELLS) { won = true; break; }
                if (grow) {
                    do {
                        food = random_cell(&seed, CELLS);
                        hit = false;
                        for (unsigned int i = 0; i < length; ++i) hit |= body[i] == food;
                    } while (hit);
                }
                step = now + (length < 22 ? 190 - length*5 : 80);
            }
            hud(frame, SNAKE, length - 3);
            box(frame, 7, 19, 226, 122, PANEL);
            box(frame, 9+(food%C)*TILE, 21+(food/C)*TILE, 5, 5, RED);
            for (unsigned int i = 0; i < length; ++i)
                box(frame, 8+(body[i]%C)*TILE, 20+(body[i]/C)*TILE, 7, 7, i ? GREEN : WHITE);
            present(frame);
            wait_until(clock, now + 32);
        }
    } while (finish(frame, clock, won));
}

#ifdef FIXFINDER_PACMAN
bool fixfinder_pacman_run(uint8_t *frame, const volatile uint32_t *clock);
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

static void maze(uint8_t *frame, const volatile uint32_t *clock)
{
    do {
        char dots[15][22];
        memcpy(dots, maze_map, sizeof dots);
        unsigned int left = 0, score = 0;
        for (int y = 0; y < 15; ++y)
            for (int x = 0; x < 21; ++x)
                left += dots[y][x] == '.' || dots[y][x] == 'o';
        int x = 10, y = 11, heading = 2, next = 2, lives = 3;
        int gx[2] = {1,19}, gy[2] = {7,7}, gd[2] = {0,2};
        uint32_t step = *clock + 130, power_until = *clock, safe_until = *clock + 900;
        unsigned int turns = 0;
        while (left && lives) {
            uint32_t now = *clock, keys = fixfinder_keys(now);
            if (fixfinder_arcade_exit(keys, now)) return;
            next = direction(keys, next);
            bool powered = (int32_t)(power_until - now) > 0;
            if ((int32_t)(now-step) >= 0) {
                if (maze_open(x+dx[next], y+dy[next])) heading = next;
                if (maze_open(x+dx[heading], y+dy[heading])) { x+=dx[heading]; y+=dy[heading]; }
                if (dots[y][x] == '.' || dots[y][x] == 'o') {
                    if (dots[y][x] == 'o') { power_until = now+5500; powered = true; }
                    dots[y][x] = ' ';
                    --left; ++score;
                }
                if (++turns % 2 == 0) for (int g = 0; g < 2; ++g) {
                    int best = 1000, move = -1;
                    for (int d = 0; d < 4; ++d) {
                        int nx = gx[g]+dx[d], ny = gy[g]+dy[d];
                        if (!maze_open(nx, ny)) continue;
                        int ax = nx-x, ay = ny-y;
                        int dist = (ax<0?-ax:ax)+(ay<0?-ay:ay);
                        int rank = (powered ? -dist : dist) + (d == ((gd[g]+2)&3) ? 80 : 0);
                        if (rank < best) { best = rank; move = d; }
                    }
                    if (move >= 0) { gd[g]=move; gx[g]+=dx[move]; gy[g]+=dy[move]; }
                }
                step = now+125;
            }
            for (int g=0; g<2; ++g) if (gx[g]==x && gy[g]==y) {
                if (powered) { gx[g]=g?19:1; gy[g]=7; score+=10; }
                else if ((int32_t)(now-safe_until)>=0) {
                    --lives; x=10; y=11; safe_until=now+1400;
                }
            }
            hud(frame, MAZE, score);
            for (int row=0; row<15; ++row) for (int col=0; col<21; ++col) {
                int px=36+col*8, py=19+row*8;
                if (dots[row][col]=='#') box(frame,px,py,8,8,BLUE);
                if (dots[row][col]=='.') box(frame,px+3,py+3,2,2,MUTED);
                if (dots[row][col]=='o') box(frame,px+2,py+2,4,4,WHITE);
            }
            box(frame,37+x*8,20+y*8,6,6,YELLOW);
            box(frame,37+x*8+(heading==0?4:0),22+y*8,2,2,BG);
            for (int g=0; g<2; ++g) box(frame,37+gx[g]*8,20+gy[g]*8,6,6,powered?MUTED:(g?PINK:RED));
            for (int i=0;i<lives;++i) box(frame,12,25+10*i,5,5,YELLOW);
            present(frame);
            wait_until(clock,now+32);
        }
        if (!finish(frame, clock, left==0)) return;
    } while (true);
}

#endif

static int clamp(int value, int lo, int hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

static void paddle_game(uint8_t *frame, const volatile uint32_t *clock, bool bricks)
{
    do {
        int player = 65, opponent = 65, x = 120, y = 85, vx = -3, vy = 2;
        unsigned int score = 0, other = 0;
        uint16_t rows[4] = {0x3ff,0x3ff,0x3ff,0x3ff};
        if (bricks) player = 105;
        while (score < (bricks ? 40U : 5U) && other < (bricks ? 3U : 5U)) {
            uint32_t now = *clock, keys = fixfinder_keys(now);
            if (fixfinder_arcade_exit(keys, now)) return;
            if (bricks) {
                if (keys == KEY_BACK) player -= 5;
                if (keys == KEY_ENTER) player += 5;
                player = clamp(player, 8, 199);
            } else {
                if (keys == KEY_ERASE) player -= 5;
                if (keys == KEY_DOWN) player += 5;
                player = clamp(player, 20, 117);
                opponent = clamp(opponent + (y < opponent+10 ? -2 : 2), 20, 117);
            }
            x += vx; y += vy;
            if (y <= 20) { y=20; vy=2; }
            if (!bricks && y >= 137) { y=137; vy=-2; }
            if (bricks) {
                if (x <= 8 || x >= 228) { x=clamp(x,8,228); vx=-vx; }
                if (vy > 0 && y >= 130 && y <= 135 && x+4 >= player && x < player+32) vy=-2;
                if (y >= 141) { ++other; x=120; y=94; vy=-2; }
                if (y >= 30 && y < 66 && x >= 19 && x < 219) {
                    int row=(y-30)/9, col=(x-19)/20;
                    if (rows[row] & (1U << col)) { rows[row] ^= 1U << col; ++score; vy=-vy; }
                }
            } else {
                if (vx<0 && x<=17 && x>=11 && y+4>=player && y<player+23) vx=3;
                if (vx>0 && x>=219 && x<=225 && y+4>=opponent && y<opponent+23) vx=-3;
                if (x<6 || x>230) {
                    if (x<6) ++other; else ++score;
                    vx = x<6 ? 3 : -3;
                    x=120; y=80;
                }
            }
            hud(frame,bricks?BREAKOUT:PONG,score);
            number(183, 2, bricks ? 3-other : other);
            box(frame, 7, 18, 226, 2, PANEL);
            box(frame, 7, 141, 226, 2, PANEL);
            if (bricks) {
                for (int row=0; row<4; ++row) for (int col=0; col<10; ++col)
                    if (rows[row] & (1U << col)) box(frame,19+col*20,30+row*9,18,7,RED+row);
                box(frame,player,134,32,4,WHITE);
            } else {
                for (int i=24;i<137;i+=9) box(frame,119,i,2,5,PANEL);
                box(frame,12,player,4,23,GREEN);
                box(frame,224,opponent,4,23,ORANGE);
            }
            box(frame,x,y,4,4,WHITE);
            present(frame);
            wait_until(clock,now+32);
        }
        if (!finish(frame,clock,score==(bricks?40U:5U))) return;
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

static uint8_t doom_art[DOOM_LOGO_WIDTH*DOOM_LOGO_HEIGHT/2] __attribute__((section(".arcade_art")));

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
    case DOOM:
        art(frame,x+2,y+1,76,47,DOOM_LOGO_WIDTH,DOOM_LOGO_HEIGHT,doom_art,DOOM_ART,13,INSET);
        break;
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
        if (game==DOOM) {
            art(frame,77,40,86,53,DOOM_LOGO_WIDTH,DOOM_LOGO_HEIGHT,doom_art,DOOM_ART,13,PANEL);
            text(120, 99, "Loading Doom", &ui_small, WHITE, PANEL, true);
        } else {
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

static void selector(uint8_t *frame, unsigned int selected, unsigned int total)
{
    memset(frame,BG,W*H);
    reset_labels(true);
    uint8_t accent=selected<GAME_COUNT?games[selected].color:GREEN;
    const char *name=selected<GAME_COUNT?games[selected].name:"";
    const char *genre=selected<GAME_COUNT?games[selected].genre:"CHIP 8";
#ifdef FIXFINDER_EMMC_LIBRARY
    if (selected>=GAME_COUNT) name=fixfinder_library_title(selected-GAME_COUNT);
#endif
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
    if (selected==DOOM) {
        art(frame,74,30,92,56,DOOM_LOGO_WIDTH,DOOM_LOGO_HEIGHT,doom_art,DOOM_ART,13,PANEL);
    } else if (selected<GAME_COUNT) {
        illustration(frame,80,34,selected);
    } else {
        box(frame,101,44,38,32,INSET);
        for (int pin=0;pin<5;++pin) {
            box(frame,96,47+pin*6,5,2,GREEN);
            box(frame,139,47+pin*6,5,2,GREEN);
        }
        text(120, 53, "8-bit", &ui_small, GREEN, INSET, true);
    }
    title(86,name);
    text(120, 105, genre, &ui_small, accent, PANEL, true);
    for (unsigned int i=0;i<total;++i)
        box(frame,120-(total*10-4)/2+i*10,129,6,3,i==selected?accent:EDGE);
    text(12, 142, "Erase  Prev", &ui_small, MUTED, BG, false);
    text(86, 142, "Down  Next", &ui_small, MUTED, BG, false);
    box(frame,166,140,62,17,accent);
    text(197, 142, "Enter  Play", &ui_small, BG, accent, true);
}

void fixfinder_arcade_run(uint8_t *frame, const volatile uint32_t *clock)
{
    unsigned int selected=0;
    unsigned int total=GAME_COUNT;
#ifdef FIXFINDER_EMMC_LIBRARY
    total+=fixfinder_library_init();
#ifdef FIXFINDER_PACMAN_EMMC
    (void)fixfinder_library_read_pacman(frame, PACMAN_ROM_SIZE);
#endif
#endif
    uint32_t previous=KEY_BACK|KEY_DOWN|KEY_ENTER|KEY_ERASE;
    memcpy(palette,base_palette,sizeof palette);
    import_colors(BRAND,logo_palette,sizeof logo_palette/sizeof *logo_palette);
    import_colors(DOOM_ART,doom_logo_palette,sizeof doom_logo_palette/sizeof *doom_logo_palette);
    unpack_art(wordmark,sizeof wordmark,logo_data);
    unpack_art(doom_art,sizeof doom_art,doom_logo_data);
    doom_display_set_palette(palette);
    bool redraw = true;
    for (;;) {
        uint32_t now=*clock, keys=fixfinder_keys(now);
        if (keys!=previous) {
            if (keys==KEY_ERASE) {
                selected=(selected+total-1)%total;
                redraw = true;
            }
            if (keys==KEY_DOWN) {
                selected=(selected+1)%total;
                redraw = true;
            }
            if (keys==KEY_ENTER && previous==0) {
#ifdef FIXFINDER_EMMC_LIBRARY
                if (selected>=GAME_COUNT) {
                    header(frame);
                    box(frame,20,43,200,76,PANEL);
                    title(62,fixfinder_library_title(selected-GAME_COUNT));
                    text(120, 90, "Loading game", &ui_small, GREEN, PANEL, true);
                    present(frame);
                    wait_until(clock,now+550);
                    if (!fixfinder_library_play(selected-GAME_COUNT,frame,clock)) {
                        text(120, 105, "Could not read game", &ui_small, RED, PANEL, true);
                        present(frame);
                        wait_until(clock,*clock+1000);
                    }
                    doom_display_set_palette(palette);
                } else
#endif
                {
                splash(frame,clock,selected);
                switch (selected) {
                case DOOM: return;
                case SNAKE: snake(frame,clock); break;
                case MAZE:
#ifdef FIXFINDER_PACMAN
                    if (!fixfinder_pacman_run(frame,clock)) {
                        doom_display_set_palette(palette);
                        text(120, 125, "Could not load Pac-Man ROM", &ui_small, RED, PANEL, true);
                        present(frame);
                        wait_until(clock,*clock+1200);
                    }
                    doom_display_set_palette(palette);
#else
                    maze(frame,clock);
#endif
                    break;
                case PONG: paddle_game(frame,clock,false); break;
                case BREAKOUT: paddle_game(frame,clock,true); break;
                case RACER: {
                    enum race_result result;
                    do {
                        result=fixfinder_race(clock);
                        if (result==RACE_EXIT) break;
                        hud(frame,RACER,0);
                    } while (finish(frame,clock,result==RACE_FINISHED));
                    break;
                }
                }
                }
                keys=fixfinder_keys(*clock);
                redraw = true;
            }
            previous=keys;
        }
        if (redraw) {
            selector(frame,selected,total);
            present(frame);
            redraw = false;
        } else {
            doom_usb_video_capture_native_rows(usb_native_row, preview_row, frame, palette);
        }
        wait_until(clock,now+1);
    }
}
