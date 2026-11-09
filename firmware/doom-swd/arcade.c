#include "arcade.h"
#include "display.h"
#include "usb_video.h"
#include "../lcd/fixfinder_io.h"
#include <string.h>

enum color { BG, WHITE, MUTED, ORANGE, GREEN, RED, BLUE, YELLOW, PINK, PANEL, EARTH };
enum game_id { DOOM, SNAKE, MAZE, PONG, BREAKOUT, BLOCKCRAFT, GAME_COUNT };
enum { W = DOOM_FRAME_WIDTH, H = DOOM_FRAME_HEIGHT };

static const uint8_t palette[768] = {
    [BG*3] = 13, 17, 25, [WHITE*3] = 248, 243, 232,
    [MUTED*3] = 148, 160, 177, [ORANGE*3] = 255, 112, 40,
    [GREEN*3] = 78, 222, 109, [RED*3] = 241, 64, 75,
    [BLUE*3] = 69, 137, 241, [YELLOW*3] = 255, 216, 68,
    [PINK*3] = 235, 115, 184, [PANEL*3] = 35, 44, 60,
    [EARTH*3] = 153, 93, 50,
};

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
    const char *hint;
    uint8_t color;
} games[] = {
    [DOOM] = {"DOOM", "E1M1 HANGAR", RED},
    [SNAKE] = {"SNAKE", "FOUR KEYS STEER", GREEN},
    [MAZE] = {"MAZE CHASE", "EAT DOTS  DODGE GHOSTS", YELLOW},
    [PONG] = {"PONG", "ERASE UP  DOWN DOWN", WHITE},
    [BREAKOUT] = {"BREAKOUT", "BACK LEFT  ENTER RIGHT", PINK},
    [BLOCKCRAFT] = {"BLOCKCRAFT", "BUILD A BLOCK WORLD", BLUE},
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

static void cube(uint8_t *frame, int x, int y, uint8_t color)
{
    for (int line=0; line<8; ++line) {
        int half = line<4 ? (line+1)*2 : (8-line)*2;
        box(frame,x-half,y+line,half*2,1,color);
        box(frame,x-half,y+line+5,half,1,EARTH);
        box(frame,x,y+line+5,half,1,PANEL);
    }
}

enum { WORLD = 12, BLOCK = 256, VIEW_TOP = 28, VIEW_HEIGHT = 116 };
static const int16_t sine[32] = {
    0,50,98,142,181,213,237,251,256,251,237,213,181,142,98,50,
    0,-50,-98,-142,-181,-213,-237,-251,-256,-251,-237,-213,-181,-142,-98,-50
};

struct ray_hit { int cell, x, y, z; };

static struct ray_hit cast(const uint8_t *terrain, int x, int y, int z,
                           int step_x, int step_y, int step_z)
{
    for (int i=0; i<64; ++i) {
        x+=step_x; y+=step_y; z+=step_z;
        if ((unsigned int)x >= WORLD*BLOCK || (unsigned int)y >= WORLD*BLOCK) break;
        int cell = (y/BLOCK)*WORLD + x/BLOCK;
        if (z < terrain[cell]*BLOCK) return (struct ray_hit){cell,x,y,z};
    }
    return (struct ray_hit){-1,x,y,z};
}

static void blockcraft(uint8_t *frame, const volatile uint32_t *clock)
{
    uint8_t terrain[WORLD*WORLD];
    for (int y=0;y<WORLD;++y) for (int x=0;x<WORLD;++x)
        terrain[y*WORLD+x] = 1 + ((x+y>14 && x>4) || (x==2 && y<5));
    int x=4*BLOCK+128, y=4*BLOCK+128;
    unsigned int angle=4;
    uint32_t previous=0;
    for (;;) {
        uint32_t now=*clock, keys=fixfinder_keys(now);
        if (fixfinder_arcade_exit(keys,now)) return;
        if (keys==KEY_BACK) angle=(angle+31)&31;
        if (keys==KEY_ENTER) angle=(angle+1)&31;
        int fx=sine[(angle+8)&31], fy=sine[angle];
        int standing=(y/BLOCK)*WORLD+x/BLOCK;
        if (keys==KEY_ERASE || keys==KEY_DOWN) {
            int sign=keys==KEY_ERASE?1:-1;
            int nx=clamp(x+sign*fx/8,32,WORLD*BLOCK-33);
            int ny=clamp(y+sign*fy/8,32,WORLD*BLOCK-33);
            int cell=(ny/BLOCK)*WORLD+nx/BLOCK;
            if (terrain[cell]<=terrain[standing]+1) { x=nx; y=ny; standing=cell; }
        }
        int z=terrain[standing]*BLOCK+BLOCK+96;
        struct ray_hit target=cast(terrain,x,y,z,fx/8,fy/8,-12);
        if (keys!=previous && target.cell>=0 && target.cell!=standing) {
            if (keys==(KEY_ERASE|KEY_DOWN) && terrain[target.cell]<7) ++terrain[target.cell];
            if (keys==(KEY_BACK|KEY_ENTER) && terrain[target.cell]>0) --terrain[target.cell];
        }
        previous=keys;
        hud(frame,BLOCKCRAFT,target.cell<0?0:terrain[target.cell]);
        text(frame,8,18,"ERASE DOWN BUILD",1,MUTED);
        text(frame,128,18,"BACK ENTER DIG",1,MUTED);
        for (int col=0;col<W/2;++col) {
            int skew=col-W/4;
            int step_x=fx/8-fy*skew/512;
            int step_y=fy/8+fx*skew/512;
            for (int row=0;row<VIEW_HEIGHT/2;++row) {
                struct ray_hit hit=cast(terrain,x,y,z,step_x,step_y,(row-VIEW_HEIGHT/4)/2-12);
                uint8_t color=BLUE;
                if (hit.cell>=0) {
                    bool top=hit.z+24>=terrain[hit.cell]*BLOCK;
                    bool edge=(hit.x&255)<14 || (hit.y&255)<14;
                    color=edge?PANEL:top?GREEN:EARTH;
                }
                box(frame,col*2,VIEW_TOP+row*2,2,2,color);
            }
        }
        box(frame,117,VIEW_TOP+VIEW_HEIGHT/2,7,1,WHITE);
        box(frame,120,VIEW_TOP+VIEW_HEIGHT/2-3,1,7,WHITE);
        present(frame);
        wait_until(clock,now+72);
    }
}

static void splash(uint8_t *frame, const volatile uint32_t *clock, enum game_id game)
{
    uint32_t start=*clock;
    do {
        uint32_t now=*clock;
        memset(frame,BG,W*H);
        text(frame,59,20,"HACKERZONE",2,ORANGE);
        box(frame,18,44,204,71,PANEL);
        box(frame,18,44,204,2,games[game].color);
        int x=(W-(int)strlen(games[game].name)*12)/2;
        text(frame,x,56,games[game].name,2,games[game].color);
        switch (game) {
        case DOOM:
            box(frame,107,78,26,4,RED);
            box(frame,116,74,8,12,RED);
            break;
        case SNAKE:
            for (int i=0;i<4;++i) box(frame,102+i*7,78,6,6,i==3?WHITE:GREEN);
            box(frame,134,79,4,4,RED);
            break;
        case MAZE:
            box(frame,103,76,9,9,YELLOW);
            box(frame,110,79,2,3,PANEL);
            for (int i=0;i<3;++i) box(frame,117+i*7,80,2,2,WHITE);
            box(frame,138,76,9,9,PINK);
            break;
        case PONG:
            box(frame,100,75,3,12,GREEN);
            box(frame,119,79,4,4,WHITE);
            box(frame,137,75,3,12,ORANGE);
            break;
        case BREAKOUT:
            for (int i=0;i<4;++i) box(frame,100+i*10,75,8,4,RED+i);
            box(frame,124,81,3,3,WHITE);
            box(frame,111,86,16,2,WHITE);
            break;
        case BLOCKCRAFT:
            cube(frame,112,74,GREEN);
            cube(frame,128,74,BLUE);
            break;
        }
        x=(W-(int)strlen(games[game].hint)*6)/2;
        text(frame,x,93,games[game].hint,1,WHITE);
        box(frame,30,126,180,6,PANEL);
        box(frame,30,126,clamp((now-start)*180/650,1,180),6,games[game].color);
        present(frame);
        wait_until(clock,now+32);
    } while (*clock-start<650U);
}

void fixfinder_arcade_run(uint8_t *frame, const volatile uint32_t *clock)
{
    unsigned int selected=0;
    uint32_t previous=KEY_BACK|KEY_DOWN|KEY_ENTER|KEY_ERASE;
    doom_display_set_palette(palette);
    for (;;) {
        uint32_t now=*clock, keys=fixfinder_keys(now);
        if (keys!=previous) {
            if (keys==KEY_ERASE) selected=(selected+GAME_COUNT-1)%GAME_COUNT;
            if (keys==KEY_DOWN) selected=(selected+1)%GAME_COUNT;
            if (keys==KEY_ENTER && previous==0) {
                splash(frame,clock,selected);
                switch (selected) {
                case DOOM: return;
                case SNAKE: snake(frame,clock); break;
                case MAZE: maze(frame,clock); break;
                case PONG: paddle_game(frame,clock,false); break;
                case BREAKOUT: paddle_game(frame,clock,true); break;
                case BLOCKCRAFT: blockcraft(frame,clock); break;
                }
                keys=fixfinder_keys(*clock);
            }
            previous=keys;
        }
        memset(frame,BG,W*H);
        text(frame,13,8,"HACKERZONE ARCADE",2,ORANGE);
        box(frame,12,26,216,1,ORANGE);
        for (unsigned int i=0;i<GAME_COUNT;++i) {
            int y=34+(int)i*17;
            if (i==selected) box(frame,12,y-2,216,15,PANEL);
            box(frame,18,y+2,5,5,games[i].color);
            text(frame,30,y,games[i].name,1,i==selected?WHITE:MUTED);
        }
        text(frame,13,145,"ERASE UP  DOWN DOWN  ENTER PLAY",1,MUTED);
        present(frame);
        wait_until(clock,now+32);
    }
}
