#include "dino.h"
#include "arcade.h"
#include "display.h"
#include "input.h"
#include "usb_input.h"
#include "usb_video.h"
#include "../lcd/fixfinder_io.h"
#include <stdlib.h>
#include <string.h>

/* Native remake of André Moreira's one-button Dino Run from the CHIP-8 Archive. */
enum { W=DOOM_LCD_WIDTH, H=DOOM_LCD_HEIGHT, GROUND=190, FIRST_MOVING_ROW=65, DINO_X=52 };
enum color { SKY_TOP, SKY_LOW, CLOUD, SUN, MESA, RIDGE, SAND, EARTH, TRAIL,
             SHADE, DINO, HIGHLIGHT, CACTUS, CACTUS_DARK, INK, PAPER };
static const uint8_t palette[768]={
    [SKY_TOP*3]=105,170,182, [SKY_LOW*3]=161,206,198, [CLOUD*3]=228,231,200,
    [SUN*3]=255,218,137, [MESA*3]=198,142,116, [RIDGE*3]=166,106,98,
    [SAND*3]=228,183,123, [EARTH*3]=205,153,101, [TRAIL*3]=171,115,79,
    [SHADE*3]=28,62,58, [DINO*3]=63,121,86, [HIGHLIGHT*3]=137,178,104,
    [CACTUS*3]=58,113,94, [CACTUS_DARK*3]=35,78,74, [INK*3]=35,53,55,
    [PAPER*3]=255,240,205,
};

static void span(uint8_t *image,int y,int left,int right,uint8_t color)
{
    if (y<0 || y>=H) return;
    if (left<0) left=0;
    if (right>W) right=W;
    if (right>left) memset(image+y*W+left,color,right-left);
}

static void rect(uint8_t *image,int x,int y,int w,int h,uint8_t color)
{
    for (int i=0;i<h;++i) span(image,y+i,x,x+w,color);
}

static void backdrop(uint8_t *image)
{
    rect(image,0,0,W,42,SKY_TOP);
    rect(image,0,42,W,H-42,SKY_LOW);
    for (int y=-23;y<=23;++y) {
        int half=23;
        while (half*half+y*y>529) --half;
        span(image,41+y,249-half,250+half,SUN);
    }
}

static void terrain(uint8_t *image,unsigned int distance)
{
    rect(image,0,FIRST_MOVING_ROW,W,GROUND-FIRST_MOVING_ROW,SKY_LOW);
    for (int i=0;i<3;++i) {
        int x=335-(int)((distance/5+i*125)%410);
        rect(image,x,75+i*6,51,4,CLOUD);
        rect(image,x+10,70+i*6,26,5,CLOUD);
    }
    for (int x=0;x<W;++x) {
        int top=136+abs((x+(int)(distance/18))%100-50)/2;
        int front=161+abs((x+(int)(distance/9)+24)%82-41)/3;
        rect(image,x,top,1,GROUND-top,MESA);
        rect(image,x,front,1,GROUND-front,RIDGE);
    }
    rect(image,0,GROUND,W,H-GROUND,SAND);
    rect(image,0,GROUND,W,2,TRAIL);
    rect(image,0,219,W,H-219,EARTH);
    for (int i=0;i<14;++i) {
        int x=340-(int)((distance+i*27)%380);
        rect(image,x,199+(i%5)*8,4+i%8,2,TRAIL);
    }
}

static void cactus(uint8_t *image,int x,int height)
{
    rect(image,x+7,GROUND-height,8,height,CACTUS);
    rect(image,x+12,GROUND-height,3,height,CACTUS_DARK);
    rect(image,x+3,GROUND-height+3,4,12,CACTUS);
    rect(image,x+3,GROUND-height+12,9,4,CACTUS);
    rect(image,x+16,GROUND-height+7,4,11,CACTUS);
    rect(image,x+13,GROUND-height+16,7,4,CACTUS_DARK);
}

static void dinosaur(uint8_t *image,int feet,unsigned int tick)
{
    static const char sprite[][16]={
        "        111111 ","       12222221","       12232221","       12222221",
        "       1111111 ","       12221   ","      122221   ","1    122222111 ",
        "11  122222221  ","12112222221    "," 122222221     ","  1111111      ",
    };
    static const uint8_t inks[]={SHADE,DINO,PAPER};
    for (int y=0;y<12;++y) for (int x=0;x<15;++x)
        if (sprite[y][x]>='1' && sprite[y][x]<='3')
            rect(image,DINO_X+x*3,feet-45+y*3,3,3,inks[sprite[y][x]-'1']);
    bool stride=feet==GROUND && (tick&4);
    rect(image,DINO_X+13,feet-10,5,stride?6:10,SHADE);
    rect(image,DINO_X+23,feet-10,5,stride?10:6,DINO);
}

static void score_digits(uint8_t *image,unsigned int score)
{
    static const uint16_t digits[]={0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
    for (int pos=4;pos>=0;--pos) {
        uint16_t bits=digits[score%10];
        for (int i=0;i<15;++i) if (bits&(1U<<(14-i)))
            rect(image,258+pos*10+i%3*2,80+i/3*2,2,2,PAPER);
        score/=10;
    }
}

static const uint8_t *lcd_row(const void *frame,unsigned int y)
{
    return (const uint8_t *)frame+y*W;
}

static const uint8_t *preview_row(const void *frame,unsigned int y)
{
    static uint8_t row[DOOM_FRAME_WIDTH] __attribute__((section(".arcade_art")));
    const uint8_t *source=lcd_row(frame,y*H/DOOM_FRAME_HEIGHT);
    for (unsigned int x=0;x<DOOM_FRAME_WIDTH;++x) row[x]=source[x*W/DOOM_FRAME_WIDTH];
    return row;
}

bool fixfinder_dino_run(const volatile uint32_t *clock,unsigned int *score)
{
    uint8_t *frame=malloc(W*H);
    if (!frame) return false;
    uint8_t *shadow=malloc(W*(H-FIRST_MOVING_ROW));
    doom_display_set_palette(palette);
    backdrop(frame);
    uint32_t step=*clock, render=step;
    unsigned int tick=0, distance=0;
    int height=0, velocity=0, cactus_x=W+25, cactus_height=30;
    bool was_jump=true, first=true, crashed=false;
    fixfinder_set_led(FIXFINDER_LED_GREEN);
    for (;;) {
        uint32_t now=*clock, keys=fixfinder_keys(now);
        if (fixfinder_arcade_exit(keys,now)) break;
        bool jump=keys!=(KEY_DOWN|KEY_BACK) && (keys || (doom_usb_input_buttons()&DOOM_BUTTON_FIRE));
        if (jump && !was_jump && !height) {
            velocity=2230;
            fixfinder_beep(22);
        }
        was_jump=jump;
        while ((int32_t)(now-step)>=0 && !crashed) {
            height+=velocity;
            if (height>0) velocity-=147;
            else height=velocity=0;
            unsigned int speed=3+(tick<1500?tick/375:4);
            distance+=speed;
            cactus_x-=(int)speed;
            if (cactus_x< -22) {
                cactus_x=W+20+(int)(tick%70);
                cactus_height=25+(int)(tick%14);
            }
            crashed=cactus_x+16>DINO_X+7 && cactus_x+5<DINO_X+39 && (height>>8)<cactus_height-3;
            ++tick;
            step+=16+(tick&1U);
        }
        if (crashed || (int32_t)(now-render)>=0) {
            terrain(frame,distance);
            cactus(frame,cactus_x,cactus_height);
            dinosaur(frame,GROUND-(height>>8),tick);
            rect(frame,252,76,58,18,INK);
            score_digits(frame,tick/6);
            unsigned int start=first?0:FIRST_MOVING_ROW;
            if (first || !shadow) doom_dcs_begin_native_rows(start);
            for (unsigned int y=start;y<H;++y) {
                const uint8_t *row=lcd_row(frame,y);
                if (first || !shadow) {
                    doom_dcs_write_native(y,row);
                    if (shadow && y>=FIRST_MOVING_ROW) memcpy(shadow+(y-FIRST_MOVING_ROW)*W,row,W);
                } else {
                    doom_dcs_update_native_row(y,row,shadow+(y-FIRST_MOVING_ROW)*W,0,W);
                }
            }
            doom_usb_video_capture_native_rows(lcd_row,preview_row,frame,palette);
            first=false;
            render=now+16;
        }
        if (crashed) break;
    }
    *score=tick/6;
    free(shadow);
    free(frame);
    return crashed;
}
