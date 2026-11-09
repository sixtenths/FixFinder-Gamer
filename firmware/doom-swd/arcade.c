#include "arcade.h"
#ifdef FIXFINDER_EMMC_LIBRARY
#include "game_library.h"
#endif
#include "display.h"
#include "usb_video.h"
#include "../lcd/fixfinder_io.h"
#include "../usb-loader/screen_art.h"
#include "../usb-loader/doom_logo.h"
#include <string.h>

enum color { BG, WHITE, MUTED, ORANGE, GREEN, RED, BLUE, YELLOW, PINK, PANEL,
             EDGE, INSET, GLYPH_LOW, GLYPH_HIGH, BRAND = 16, DOOM_ART = 32 };
enum game_id { DOOM, SNAKE, MAZE, PONG, BREAKOUT, GAME_COUNT };
enum { W = DOOM_FRAME_WIDTH, H = DOOM_FRAME_HEIGHT };

static const uint8_t base_palette[768] = {
    [BG*3] = 15, 17, 23, [WHITE*3] = 248, 243, 232,
    [MUTED*3] = 148, 160, 177, [ORANGE*3] = 255, 112, 40,
    [GREEN*3] = 78, 222, 109, [RED*3] = 241, 64, 75,
    [BLUE*3] = 69, 137, 241, [YELLOW*3] = 255, 216, 68,
    [PINK*3] = 235, 115, 184, [PANEL*3] = 29, 34, 46,
    [EDGE*3] = 62, 70, 85, [INSET*3] = 20, 23, 32,
    [GLYPH_LOW*3] = 103, 104, 109, [GLYPH_HIGH*3] = 175, 173, 170,
};
static uint8_t palette[sizeof base_palette] __attribute__((section(".arcade_art")));

static const uint8_t font[][5] = {
    {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},
    {127,65,65,34,28},{127,73,73,73,65},{127,9,9,9,1},
    {62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},
    {32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
    {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},
    {127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},
    {38,73,73,73,50},{1,1,127,1,1},{63,64,64,64,63},
    {31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
    {7,8,112,8,7},{97,81,73,69,67},
    {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},
    {33,65,69,75,49},{24,20,18,127,16},{39,69,69,69,57},
    {60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},
};

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
};

static const int dx[] = {1, 0, -1, 0};
static const int dy[] = {0, 1, 0, -1};

void doom_dcs_present(const uint8_t *frame);

static void box(uint8_t *frame, int x, int y, int width, int height, uint8_t color)
{
    if (x < 0 || y < 0 || x + width > W || y + height > H) return;
    for (int row = 0; row < height; ++row) {
        memset(frame + (y + row) * W + x, color, width);
    }
}

static void text(uint8_t *frame, int x, int y, const char *s, int scale, uint8_t color)
{
    for (; *s; ++s, x += 6 * scale) {
        int index = *s >= 'A' && *s <= 'Z' ? *s - 'A' :
                    *s >= '0' && *s <= '9' ? 26 + *s - '0' : -1;
        if (index < 0) continue;
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (font[index][col] & (1U << row))
                    box(frame, x + col*scale, y + row*scale, scale, scale, color);
    }
}

static void number(uint8_t *frame, int x, int y, unsigned int value)
{
    char digits[4] = {
        '0' + value/100%10, '0' + value/10%10, '0' + value%10, 0
    };
    text(frame, x, y, digits, 1, WHITE);
}

static void present(uint8_t *frame)
{
    doom_dcs_present(frame);
    doom_usb_video_capture(frame, palette);
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
    text(frame, 8, 6, games[game].name, 1, games[game].color);
    number(frame, 211, 6, score);
    text(frame, 8, 150, "HOLD DOWN AND BACK FOR MENU", 1, MUTED);
}

static bool finish(uint8_t *frame, const volatile uint32_t *clock, bool won)
{
    box(frame, 40, 55, 160, 49, PANEL);
    text(frame, 54, 63, won ? "YOU WIN" : "GAME OVER", 2, WHITE);
    text(frame, 54, 88, "ENTER TO PLAY AGAIN", 1, MUTED);
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
void fixfinder_pacman_run(uint8_t *frame, const volatile uint32_t *clock);
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
            number(frame, 183, 6, bricks ? 3-other : other);
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

static uint8_t wordmark[LOGO_WIDTH*LOGO_HEIGHT/2] __attribute__((section(".arcade_art")));
static uint8_t doom_art[DOOM_LOGO_WIDTH*DOOM_LOGO_HEIGHT/2] __attribute__((section(".arcade_art")));

static void art(uint8_t *frame, int x, int y, int w, int h,
                int src_w, int src_h, const uint8_t *packed, uint8_t first,
                uint8_t transparent, uint8_t paper)
{
    for (int row=0; row<h; ++row) for (int col=0; col<w; ++col) {
        int i=(row*src_h/h)*src_w+col*src_w/w;
        unsigned int index=(packed[i/2]>>((i&1)*4))&15;
        frame[(y+row)*W+x+col]=(index>=transparent || (first==BRAND && !index))?paper:first+index;
    }
}

static void title(uint8_t *frame, int center, int y, const char *name)
{
    int width=0;
    for (const char *s=name; *s; ++s) {
        unsigned int w=*s>='A'&&*s<='Z'?large_width[*s-'A']:0;
        width+=*s==' '?5:w?w+1:12;
    }
    if (width>W-16) {
        text(frame,center-((int)strlen(name)*6-1)/2,y+3,name,1,WHITE);
        return;
    }
    int x=center-width/2;
    static const uint8_t shades[]={PANEL,GLYPH_LOW,GLYPH_HIGH,WHITE};
    for (; *name; ++name) {
        if (*name==' ') { x+=5; continue; }
        int letter=*name>='0'&&*name<='9'?26+*name-'0':*name-'A';
        int w=letter<26?large_width[letter]:0;
        if (!w) {
            /* The loader's subset has no Q or Z. Scale the matching small glyph. */
            for (int row=0; row<(int)LARGE_HEIGHT; ++row)
                for (int col=0; col<10; ++col)
                    if (font[letter][col/2] & (1U << (row*7/LARGE_HEIGHT)))
                        frame[(y+row)*W+x+col]=WHITE;
            x+=12;
            continue;
        }
        const uint8_t *pixels=large_glyphs+large_offset[letter];
        for (int row=0; row<(int)LARGE_HEIGHT; ++row) for (int col=0; col<w; ++col) {
            int i=row*w+col;
            unsigned int shade=(pixels[i/4]>>((i&3)*2))&3;
            if (shade) frame[(y+row)*W+x+col]=shades[shade];
        }
        x+=w+1;
    }
}

static void centered(uint8_t *frame, int x, int y, const char *s, uint8_t color)
{
    text(frame,x-((int)strlen(s)*6-1)/2,y,s,1,color);
}

static void header(uint8_t *frame)
{
    memset(frame,BG,W*H);
    art(frame,10,8,130,11,LOGO_WIDTH,LOGO_HEIGHT,wordmark,BRAND,16,BG);
    box(frame,165,6,1,15,EDGE);
    text(frame,178,10,"ARCADE",1,WHITE);
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
            centered(frame,120,101,"LOADING DOOM",WHITE);
        } else {
            box(frame,80,39,80,49,INSET);
            illustration(frame,80,39,game);
            title(frame,120,93,games[game].name);
        }
        centered(frame,120,116,games[game].hint,MUTED);
        box(frame,20,144,200,4,PANEL);
        box(frame,20,144,clamp((now-start)*200/LOAD_MS,1,200),4,games[game].color);
        present(frame);
        wait_until(clock,now+32);
    } while (*clock-start<LOAD_MS);
}

static void selector(uint8_t *frame, unsigned int selected, unsigned int total)
{
    memset(frame,BG,W*H);
    art(frame,12,8,116,10,LOGO_WIDTH,LOGO_HEIGHT,wordmark,BRAND,16,BG);
    text(frame,176,9,"ARCADE",1,WHITE);
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
        centered(frame,120,57,"8 BIT",GREEN);
    }
    title(frame,120,89,name);
    centered(frame,120,108,genre,accent);
    for (unsigned int i=0;i<total;++i)
        box(frame,120-(total*10-4)/2+i*10,129,6,3,i==selected?accent:EDGE);
    text(frame,12,146,"ERASE PREV",1,MUTED);
    text(frame,92,146,"DOWN NEXT",1,MUTED);
    box(frame,166,140,62,17,accent);
    centered(frame,197,145,"ENTER PLAY",BG);
}

void fixfinder_arcade_run(uint8_t *frame, const volatile uint32_t *clock)
{
    unsigned int selected=0;
    unsigned int total=GAME_COUNT;
#ifdef FIXFINDER_EMMC_LIBRARY
    total+=fixfinder_library_init();
#endif
    uint32_t previous=KEY_BACK|KEY_DOWN|KEY_ENTER|KEY_ERASE;
    memcpy(palette,base_palette,sizeof palette);
    import_colors(BRAND,logo_palette,sizeof logo_palette/sizeof *logo_palette);
    import_colors(DOOM_ART,doom_logo_palette,sizeof doom_logo_palette/sizeof *doom_logo_palette);
    unpack_art(wordmark,sizeof wordmark,logo_data);
    unpack_art(doom_art,sizeof doom_art,doom_logo_data);
    doom_display_set_palette(palette);
    for (;;) {
        uint32_t now=*clock, keys=fixfinder_keys(now);
        if (keys!=previous) {
            if (keys==KEY_ERASE) selected=(selected+total-1)%total;
            if (keys==KEY_DOWN) selected=(selected+1)%total;
            if (keys==KEY_ENTER && previous==0) {
#ifdef FIXFINDER_EMMC_LIBRARY
                if (selected>=GAME_COUNT) {
                    header(frame);
                    box(frame,20,43,200,76,PANEL);
                    title(frame,120,65,fixfinder_library_title(selected-GAME_COUNT));
                    centered(frame,120,93,"LOADING GAME",GREEN);
                    present(frame);
                    wait_until(clock,now+550);
                    if (!fixfinder_library_play(selected-GAME_COUNT,frame,clock)) {
                        centered(frame,120,109,"COULD NOT READ GAME",RED);
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
                    fixfinder_pacman_run(frame,clock);
                    doom_display_set_palette(palette);
#else
                    maze(frame,clock);
#endif
                    break;
                case PONG: paddle_game(frame,clock,false); break;
                case BREAKOUT: paddle_game(frame,clock,true); break;
                }
                }
                keys=fixfinder_keys(*clock);
            }
            previous=keys;
        }
        selector(frame,selected,total);
        present(frame);
        wait_until(clock,now+32);
    }
}
