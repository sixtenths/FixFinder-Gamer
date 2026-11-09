/* Midway Pac-Man board mapping and scanout adapted from Adafruit Arcade Machines
 * (John Park for Adafruit Industries, MIT), revision 4e8d03d849f26829884066bd3d97012e94d4925b.
 * Z80 interpreter license and attribution are in pacman/LICENSE.
 */
#include "arcade.h"
#include "display.h"
#include "usb_video.h"
#include "../lcd/fixfinder_io.h"
#include "pacman/z80.h"
#include <string.h>
#include <stdlib.h>
#ifdef FIXFINDER_PACMAN_WAVETABLE
#include "gd32f4xx.h"
#endif

#ifdef FIXFINDER_PACMAN_EMMC
#include "game_library.h"
static const uint8_t *pacman_rom;
#else
extern const uint8_t pacman_rom[];
#endif

enum {
    W=DOOM_LCD_WIDTH, H=DOOM_LCD_HEIGHT, VIEW=186,
    GFX=0x4000, COLOR=0x6000, LOOKUP=0x6020, PAUSE_BG=32, PAUSE_INK=33,
    GAME_MODE=0x200, LIVES=0x214
};
static struct {
    z80 cpu;
    uint8_t video[1024], colors[1024], ram[1024], pos[16];
    uint8_t in0, in1, vector, sound[32];
    bool irq, flip, sound_enabled;
    uint64_t dirty_columns;
} board __attribute__((section(".arcade_art")));
static uint8_t rgb[768] __attribute__((section(".arcade_art")));

static uint8_t bus_read(void *state, uint16_t addr)
{
    (void)state;
    addr &= 0x7fff; /* A15 is unwired on the Midway PCB. */
    if (addr<0x4000) return pacman_rom[addr];
    if (addr<0x4400) return board.video[addr-0x4000];
    if (addr<0x4800) return board.colors[addr-0x4400];
    if (addr<0x4c00) return 0xbf;
    if (addr<0x5000) return board.ram[addr-0x4c00];
    if (addr<0x5040) return board.in0;
    if (addr<0x5080) return board.in1;
    if (addr<0x50c0) return 0xc9; /* 1 coin, 3 lives, normal difficulty. */
    return 0xff;
}

static void bus_write(void *state, uint16_t addr, uint8_t value)
{
    (void)state;
    addr &= 0x7fff;
    if (addr<0x4000) return;
    if (addr<0x4800) {
        unsigned int tile=addr&1023;
        uint8_t *bytes=addr<0x4400?board.video:board.colors;
        if (bytes[tile]!=value) {
            bytes[tile]=value;
            /* Inverse of tile_pen's rotated tilemap. The first and last two
             * memory rows hold the four columns around the center grid. */
            unsigned int row=tile>>5;
            unsigned int column=row<2?34+row:row>=30?row-30:(tile&31)+2;
            board.dirty_columns|=UINT64_C(1)<<column;
        }
    }
    else if (addr>=0x4c00 && addr<0x5000) board.ram[addr-0x4c00]=value;
    else if (addr==0x5000) board.irq=value&1;
    else if (addr==0x5001) board.sound_enabled=value&1;
    else if (addr==0x5003 && board.flip!=(bool)(value&1)) {
        board.flip=value&1;
        board.dirty_columns=UINT64_MAX;
    }
    else if (addr>=0x5040 && addr<0x5060) board.sound[addr-0x5040]=value&15;
    else if (addr>=0x5060 && addr<0x5070) board.pos[addr-0x5060]=value;
}

static uint32_t voice_increment(unsigned int voice)
{
    const uint8_t *reg=&board.sound[0x11+5*voice];
    uint32_t increment=(reg[3]<<16)|(reg[2]<<12)|(reg[1]<<8)|(reg[0]<<4);
    if (!voice) increment|=board.sound[0x10];
    return increment;
}

#ifdef FIXFINDER_PACMAN_WAVETABLE
extern const uint8_t pacman_sound_prom[256];
static struct voice {
    uint32_t step;
    unsigned int base, volume;
} voices[3];
static uint32_t phases[3];

static int16_t pacman_sample(void)
{
    int mix=0;
    for (unsigned int i=0; i<3; ++i) {
        if (voices[i].volume) {
            unsigned int sample=voices[i].base+((phases[i]>>15)&31U);
            mix+=((int)(pacman_sound_prom[sample]&15U)-8)*(int)voices[i].volume;
            phases[i]+=voices[i].step;
        }
    }
    return (int16_t)mix;
}

static void update_sound(void)
{
    struct voice next[3];
    unsigned int active=0;
    for (unsigned int i=0; i<3; ++i) {
        /* The original 20-bit phase accumulator is clocked at 96 kHz. */
        next[i].step=voice_increment(i)*(96000U/FIXFINDER_PCM_RATE);
        next[i].base=(board.sound[5+5*i]&7U)*32;
        next[i].volume=board.sound_enabled && next[i].step ? board.sound[0x15+5*i] : 0;
        active|=next[i].volume;
    }
    if (!active) {
        fixfinder_pcm_stop();
        return;
    }
    uint32_t irq=__get_PRIMASK();
    __disable_irq();
    memcpy(voices,next,sizeof voices);
    __set_PRIMASK(irq);
    fixfinder_pcm_start(pacman_sample);
}

static void stop_sound(void) { fixfinder_pcm_stop(); }
#else
static uint32_t voice_pitch(unsigned int voice)
{
    /* A 20-bit phase accumulator at 96 kHz. Play one octave higher on the beeper. */
    return (voice_increment(voice)*375U+1024U)>>11;
}

static uint32_t beeper_pitch(void)
{
    if (!board.sound_enabled) return 0;

    /* Voice 1 uses waveform 0 for the opening melody. Keep its written rests
     * quiet so the accompaniment does not interrupt the tune. */
    if (!(board.sound[0x0a]&7) && board.sound[0x1a]) {
        uint32_t hz=voice_pitch(1);
        return hz>4000?4000:hz;
    }

    /* The beeper plays the loudest active Namco voice as a square wave. */
    unsigned int volume=0;
    uint32_t pitch=0;
    for (unsigned int voice=0; voice<3; ++voice) {
        unsigned int level=board.sound[0x15+5*voice];
        uint32_t hz=voice_pitch(voice);
        if (hz>=80 && level>volume) {
            volume=level;
            pitch=hz>4000?4000:hz;
        }
    }
    return pitch;
}

static void update_sound(void) { fixfinder_tone(beeper_pitch()); }
static void stop_sound(void) { fixfinder_tone(0); }
#endif

static uint8_t port_in(z80 *cpu, uint8_t port)
{
    (void)cpu; (void)port;
    return 0xff;
}

static void port_out(z80 *cpu, uint8_t port, uint8_t value)
{
    (void)cpu;
    if (!port) board.vector=value;
}

static unsigned int pen(unsigned int attr, unsigned int pixel)
{
    return pacman_rom[LOOKUP+(attr&31)*4+pixel]&15;
}

static unsigned int pixel2(unsigned int byte, unsigned int x)
{
    unsigned int bit=3-(x&3);
    return ((byte>>(bit+3))&2)|((byte>>bit)&1);
}

static unsigned int tile_pen(int nx, int ny)
{
    int col=(nx/8-2)&255, row=ny/8+2, tx=nx&7;
    int tile=(col&32)?row+((col&31)*32):col+row*32;
    unsigned int offset=board.video[tile]*16+(tx<4?8:0)+(ny&7);
    return pen(board.colors[tile],pixel2(pacman_rom[GFX+offset],tx));
}

static void render(uint8_t *frame, bool first, uint8_t *previous, uint8_t *background, bool paused)
{
    static const int sprite_col[]={8,16,24,0};
    uint8_t line[W]={0};
    unsigned int preview_y=0;
    const unsigned int x_start=first?0:(W-VIEW)/2, width=first?W:VIEW;
    if (first || !previous) doom_dcs_begin_native_span(x_start,width);
    doom_usb_video_native_begin(rgb);
    for (int y=0; y<H; ++y) {
        if (paused && y>=12 && y<44) {
            memset(line+270,PAUSE_BG,32);
            if (y>=19 && y<37) {
                memset(line+279,PAUSE_INK,5);
                memset(line+288,PAUSE_INK,5);
            }
        } else if (paused && y==44) {
            memset(line+270,0,32);
        }
        int nx=y*288/H;
        if (board.flip) nx=287-nx;
        int nx2=((y+1)*288-1)/H;
        if (board.flip) nx2=287-nx2;
        uint8_t *out=line+(W-VIEW)/2;
        uint64_t columns=(UINT64_C(1)<<(nx/8))|(UINT64_C(1)<<(nx2/8));
        if (!background || first || (board.dirty_columns&columns)) {
            for (int x=0; x<VIEW; ++x) {
                int ny=223-x*224/VIEW;
                int ny2=223-((x+1)*224-1)/VIEW;
                if (board.flip) ny=223-ny;
                if (board.flip) ny2=223-ny2;
                /* Preserve one-pixel lettering and maze details when shrinking the raster. */
                unsigned int p=tile_pen(nx,ny);
                if (!p && ny!=ny2) p=tile_pen(nx,ny2);
                if (!p && nx!=nx2) p=tile_pen(nx2,ny);
                if (!p && nx!=nx2 && ny!=ny2) p=tile_pen(nx2,ny2);
                out[x]=p;
            }
            if (background) memcpy(background,out,VIEW);
        } else {
            memcpy(out,background,VIEW);
        }
        if (background) background+=VIEW;
        for (int s=7; nx>=16 && nx<=271 && s>=0; --s) {
            int offs=s*2;
            unsigned int info=board.ram[0x3f0+offs], attr=board.ram[0x3f1+offs];
            int sx=272-board.pos[offs+1], sy=board.pos[offs]-31+(s<3);
            int px=nx-sx;
            if (px<0) px+=256;
            if (px<0 || px>=16) continue;
            if (info&1) px=15-px;
            unsigned int base=GFX+0x1000+(info>>2)*64+sprite_col[px/4];
            for (int x=0; x<VIEW; ++x) {
                int ny=(board.flip?x*224/VIEW:223-x*224/VIEW)-sy;
                if (ny<0 || ny>=16) continue;
                if (info&2) ny=15-ny;
                unsigned int color=pen(attr,pixel2(pacman_rom[base+ny+(ny>=8?24:0)],px));
                if (color) out[x]=color;
            }
        }
        if (first || !previous) {
            doom_dcs_write_native_span(y,line,x_start,width);
        } else if (memcmp(previous,out,VIEW)!=0) {
            unsigned int left=0, right=VIEW;
            while (previous[left]==out[left]) ++left;
            while (previous[right-1]==out[right-1]) --right;
            doom_dcs_write_native_dirty_row(y,line,x_start+left,right-left);
        }
        if (previous) {
            memcpy(previous,out,VIEW);
            previous+=VIEW;
        }
        doom_usb_video_native_row(y,line);
        if (preview_y<DOOM_FRAME_HEIGHT && preview_y*H/DOOM_FRAME_HEIGHT==(unsigned int)y) {
            for (unsigned int x=0; x<DOOM_FRAME_WIDTH; ++x)
                frame[preview_y*DOOM_FRAME_WIDTH+x]=line[x*W/DOOM_FRAME_WIDTH];
            ++preview_y;
        }
    }
    board.dirty_columns=0;
    doom_usb_video_native_end();
}

static void play_pacman(uint8_t *frame, const volatile uint32_t *clock)
{
    fixfinder_set_led(FIXFINDER_LED_YELLOW);
#ifdef FIXFINDER_PACMAN_WAVETABLE
    memset(phases,0,sizeof phases);
#endif
    memset(&board,0,sizeof board);
    memset(rgb,0,sizeof rgb);
    memset(frame,0,DOOM_FRAME_WIDTH*DOOM_FRAME_HEIGHT);
    for (int i=0; i<32; ++i) {
        unsigned int c=pacman_rom[COLOR+i];
        rgb[3*i]=33*(c&1)+71*((c>>1)&1)+151*((c>>2)&1);
        rgb[3*i+1]=33*((c>>3)&1)+71*((c>>4)&1)+151*((c>>5)&1);
        rgb[3*i+2]=81*((c>>6)&1)+174*(c>>7);
    }
    const uint8_t pause_colors[]={28,33,54, 255,210,82};
    memcpy(rgb+3*PAUSE_BG,pause_colors,sizeof pause_colors);
    doom_display_set_palette(rgb);
    z80_init(&board.cpu);
    board.cpu.read_byte=bus_read;
    board.cpu.write_byte=bus_write;
    board.cpu.port_in=port_in;
    board.cpu.port_out=port_out;
    uint8_t *previous=malloc(VIEW*H);
    uint8_t *background=malloc(VIEW*H);
    uint32_t step=*clock, cycle=0, frames=0, restart=0, attract_frames=0;
    uint32_t damage_until=0;
    unsigned int previous_lives=0;
    bool was_start=false, ready=false, first_draw=true, was_pause=false, paused=false, played=false;
    for (;;) {
        uint32_t now=*clock, keys=fixfinder_keys(now);
        if (fixfinder_arcade_exit(keys,now)) {
            stop_sound();
            free(previous);
            free(background);
            return;
        }
        bool pause=(keys==(KEY_ENTER|KEY_BACK));
        bool toggle=ready && pause && !was_pause;
        was_pause=pause;
        if (toggle) {
            paused=!paused;
            stop_sound();
            if (paused) fixfinder_set_led(FIXFINDER_LED_YELLOW);
            render(frame,true,previous,background,paused);
            first_draw=false;
            step=*clock;
        }
        bool start=(keys==(KEY_ERASE|KEY_ENTER));
        if (!paused && ready && start && !was_start) restart=frames+1;
        was_start=start;
        if (paused) {
            if (!toggle && doom_usb_video_native_requested())
                render(frame,false,previous,background,true);
            doom_usb_video_capture(frame,rgb);
            while (*clock==now) {}
            continue;
        }
        board.in0=0xff;
        if (keys==KEY_ERASE) board.in0&=~1;
        if (keys==KEY_BACK) board.in0&=~2;
        if (keys==KEY_ENTER) board.in0&=~4;
        if (keys==KEY_DOWN) board.in0&=~8;
        /* Insert a coin and start automatically after the board self-test.
         * Erase + Enter repeats that sequence if the player wants a new game.
         */
        /* $4E00/$4E01 are the Midway ROM's game/intro modes. During the
         * self-test, RAM gets overwritten with transient test patterns. */
        attract_frames=board.ram[GAME_MODE]==1 && !board.ram[0x201]?attract_frames+1:0;
        if (!ready && (attract_frames>=20 || frames==700)) {
            ready=true;
            restart=frames+1;
        }
        uint32_t elapsed=restart?frames-restart:100;
        if (elapsed<8) board.in0&=~0x20;
        board.in1=elapsed>=24 && elapsed<54?0xdf:0xff;
        cycle+=50688;
        while ((int32_t)(board.cpu.cyc-cycle)<0) z80_step(&board.cpu);
        if (board.irq) z80_gen_int(&board.cpu,board.vector);
        if (ready) {
            update_sound();
            uint8_t mode=board.ram[GAME_MODE];
            if (mode==3) {
                played=true;
                unsigned int lives=board.ram[LIVES];
                if (lives<previous_lives) damage_until=frames+36;
                previous_lives=lives;
                fixfinder_set_led(lives<=1 || (int32_t)(damage_until-frames)>0 ? FIXFINDER_LED_RED :
                                  lives==2 ? FIXFINDER_LED_YELLOW : FIXFINDER_LED_GREEN);
            } else {
                previous_lives=0;
                damage_until=frames;
                fixfinder_set_led(played && mode==1 ? FIXFINDER_LED_RED : FIXFINDER_LED_YELLOW);
            }
        }
        if (ready && !(frames&1) && (int32_t)(*clock-step)<17) {
            render(frame,first_draw,previous,background,false);
            first_draw=false;
            doom_usb_video_capture(frame,rgb);
        }
        ++frames;
        step+=16+(frames&1);
        if ((int32_t)(*clock-step)>50) step=*clock;
        while ((int32_t)(*clock-step)<0) {}
    }
}

bool fixfinder_pacman_run(uint8_t *frame, const volatile uint32_t *clock)
{
#ifdef FIXFINDER_PACMAN_EMMC
    uint8_t *rom = malloc(PACMAN_ROM_SIZE);
    if (!rom) return false;
    if (!fixfinder_library_read_pacman(rom, PACMAN_ROM_SIZE)) {
        free(rom);
        return false;
    }
    pacman_rom = rom;
#endif
    play_pacman(frame, clock);
#ifdef FIXFINDER_PACMAN_EMMC
    pacman_rom = NULL;
    free(rom);
#endif
    return true;
}
