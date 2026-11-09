#include "racer.h"
#include "arcade.h"
#include "display.h"
#include "input.h"
#include "usb_input.h"
#include "usb_video.h"
#include "../lcd/fixfinder_io.h"
#include <stdlib.h>
#include <string.h>

enum { W=DOOM_LCD_WIDTH, H=DOOM_LCD_HEIGHT, HORIZON=92, LENGTH=12000, TIME_LIMIT=48000 };
enum color {
    NIGHT, STAR, SUN, SUN_LOW, RIDGE, MOUNTAIN, CITY, WINDOW,
    LAND, LAND_ALT, ROAD, ROAD_ALT, STRIPE, EDGE, EDGE_ALT,
    SHADOW, RUBBER, GLASS, CYAN, BLUE, PINK, MAGENTA, GOLD, LIGHT, PANEL, SKY=32
};

static const uint8_t colors[768] = {
    [NIGHT*3]=10,9,28, [STAR*3]=140,130,197, [SUN*3]=255,189,106,
    [SUN_LOW*3]=249,90,145, [RIDGE*3]=65,33,92, [MOUNTAIN*3]=38,28,73,
    [CITY*3]=22,20,47, [WINDOW*3]=144,77,128, [LAND*3]=70,30,85,
    [LAND_ALT*3]=84,35,100, [ROAD*3]=36,43,67, [ROAD_ALT*3]=43,49,76,
    [STRIPE*3]=141,168,208, [EDGE*3]=247,70,165, [EDGE_ALT*3]=102,53,137,
    [SHADOW*3]=22,21,40, [RUBBER*3]=12,14,25, [GLASS*3]=30,51,83,
    [CYAN*3]=75,245,248, [BLUE*3]=34,142,198, [PINK*3]=255,103,175,
    [MAGENTA*3]=167,46,118, [GOLD*3]=255,171,66, [LIGHT*3]=235,249,248,
    [PANEL*3]=18,18,35,
    [SKY*3]=19,16,49, 23,17,54, 27,17,59, 31,18,65, 36,18,70,
    41,19,76, 47,20,81, 53,21,88, 60,22,94, 67,24,101,
    75,26,107, 84,28,114, 94,31,120, 105,35,126, 118,40,133, 132,46,140,
};

struct rival { int distance, lane; bool hit; };
struct race {
    int distance, lane, speed;
    uint32_t ticks;
    unsigned int flash;
    struct rival traffic[4];
};

static int clamp(int value, int lo, int hi)
{
    return value<lo?lo:value>hi?hi:value;
}

static void span(uint8_t *image,int y,int left,int right,uint8_t ink)
{
    left=clamp(left,0,W);
    right=clamp(right,0,W);
    if (y>=0 && y<H && left<right) memset(image+y*W+left,ink,right-left);
}

static void rect(uint8_t *image,int x,int y,int width,int height,uint8_t ink)
{
    for (int row=0;row<height;++row) span(image,y+row,x,x+width,ink);
}

static void trapezoid(uint8_t *image,int x,int y,int height,int top,int bottom,uint8_t ink)
{
    for (int row=0;row<height;++row) {
        int half=top+(bottom-top)*row/height;
        span(image,y+row,x-half,x+half+1,ink);
    }
}

static void skyline(uint8_t *image)
{
    for (int y=0;y<HORIZON;++y) span(image,y,0,W,SKY+y*16/HORIZON);
    unsigned int seed=1979;
    for (int i=0;i<50;++i) {
        seed=seed*1664525U+1013904223U;
        rect(image,(seed>>16)%W,(seed>>24)%52,1,1,STAR);
    }
    for (int dy=-30;dy<=30;++dy) {
        int radius=30;
        while (radius*radius+dy*dy>900) --radius;
        if (dy<5 || dy%6<3) span(image,49+dy,234-radius,235+radius,dy<5?SUN:SUN_LOW);
    }
    for (int x=0;x<W;++x) {
        int ridge=55+abs((x+15)%94-47)/2+abs((x+14)%47-23)/3;
        int hill=72+abs((x+63)%117-58)/4;
        rect(image,x,ridge,1,HORIZON-ridge,RIDGE);
        rect(image,x,hill,1,HORIZON-hill,MOUNTAIN);
    }
    for (int x=3;x<W;x+=11) {
        seed=seed*1664525U+1013904223U;
        int top=HORIZON-5-(int)((seed>>20)%17);
        rect(image,x,top,8,HORIZON-top,CITY);
        for (int y=top+3;y<HORIZON-2;y+=5)
            if (((seed>>((y-top)%22))&3)==1) rect(image,x+2,y,2,2,WINDOW);
    }
}

static int curve(int distance)
{
    static const int16_t bends[]={0,0,122,208,32,-166,-150,52,184,68,-130,-182,-22,130,46,0};
    unsigned int segment=(unsigned int)distance/1024;
    if (segment>=sizeof bends/sizeof *bends-1) return 0;
    int t=distance%1024;
    return bends[segment]+(bends[segment+1]-bends[segment])*t/1024;
}

static int road_half(int y)
{
    return 8+(y-HORIZON)*142/(H-HORIZON);
}

static int road_center(const struct race *state,int y,int depth,int origin)
{
    return W/2+(curve(state->distance+depth)-origin)*(y-HORIZON)/(H-HORIZON)
           -state->lane*road_half(y)/256;
}

static void road(uint8_t *image,const struct race *state,int origin)
{
    for (int y=HORIZON;y<H;++y) {
        int depth=21120/(y-HORIZON+6);
        int half=road_half(y), center=road_center(state,y,depth,origin);
        bool stripe=((state->distance+depth)/110)&1;
        int curb=1+half/22;
        span(image,y,0,W,stripe?LAND:LAND_ALT);
        span(image,y,center-half-curb,center+half+curb,stripe?EDGE:EDGE_ALT);
        span(image,y,center-half,center+half,stripe?ROAD:ROAD_ALT);
        if (stripe) {
            int marker=1+half/60;
            for (int side=-1;side<=1;side+=2) {
                int x=center+side*half/3;
                span(image,y,x-marker/2,x+(marker+1)/2,STRIPE);
            }
        }
        if (state->distance+depth>=LENGTH && state->distance+depth<LENGTH+48) {
            int tile=1+half/6;
            for (int x=center-half;x<center+half;x+=tile*2)
                span(image,y,x,clamp(x+tile,x,center+half),LIGHT);
        }
    }
}

static void car(uint8_t *image,int x,int y,int size,uint8_t paint,uint8_t trim)
{
    int height=2+size/2;
    span(image,y+1,x-size-3,x+size+4,SHADOW);
    trapezoid(image,x,y-height,height/2,1+size/3,2+size*2/3,paint);
    trapezoid(image,x,y-height+1,1+height/3,size/4,size/2,GLASS);
    rect(image,x-size,y-height/2,size*2+1,height/2,trim);
    rect(image,x-size,y-height/3,1+size/5,height/3+1,RUBBER);
    rect(image,x+size-size/5,y-height/3,1+size/5,height/3+1,RUBBER);
    span(image,y-height/2,x-size+1,x+size,paint);
    span(image,y-height/3,x-size/2-1,x-size/5,PINK);
    span(image,y-height/3,x+size/5+1,x+size/2+2,PINK);
    span(image,y-1,x-size/3,x+size/3+1,SHADOW);
}

static void digits(uint8_t *image,int x,int y,unsigned int number,unsigned int count,uint8_t ink)
{
    static const uint8_t patterns[]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    static const uint8_t bars[][4]={{2,0,6,2},{8,2,2,5},{8,8,2,5},{2,13,6,2},
                                     {0,8,2,5},{0,2,2,5},{2,6,6,2}};
    for (unsigned int i=0;i<count;++i) {
        uint8_t bits=patterns[number%10];
        int left=x+(count-1-i)*12;
        for (unsigned int b=0;b<7;++b) if (bits&(1U<<b))
            rect(image,left+bars[b][0],y+bars[b][1],bars[b][2],bars[b][3],ink);
        number/=10;
    }
}

static const uint8_t *lcd_row(const void *image,unsigned int y)
{
    return (const uint8_t *)image+y*W;
}

static const uint8_t *preview_row(const void *image,unsigned int y)
{
    static uint8_t row[DOOM_FRAME_WIDTH];
    const uint8_t *source=lcd_row(image,y*H/DOOM_FRAME_HEIGHT);
    for (unsigned int x=0;x<DOOM_FRAME_WIDTH;++x) row[x]=source[x*W/DOOM_FRAME_WIDTH];
    return row;
}

static void draw(uint8_t *image,const struct race *state,uint32_t time,bool first)
{
    int origin=curve(state->distance);
    road(image,state,origin);
    int order[]={0,1,2,3};
    for (int i=1;i<4;++i) for (int j=i;j && state->traffic[order[j]].distance>state->traffic[order[j-1]].distance;--j) {
        int index=order[j]; order[j]=order[j-1]; order[j-1]=index;
    }
    for (int i=0;i<4;++i) {
        int index=order[i];
        const struct rival *rival=&state->traffic[index];
        int depth=rival->distance-state->distance;
        if (depth<153 || depth>1508) continue;
        int y=HORIZON+21120/depth-6;
        int x=road_center(state,y,depth,origin)+rival->lane*road_half(y)/256;
        car(image,x,y,2+road_half(y)/5,index&1?GOLD:PINK,index&1?EDGE_ALT:MAGENTA);
    }
    car(image,160,221,42,state->flash?LIGHT:CYAN,state->flash?PINK:BLUE);
    if (state->speed>128) {
        rect(image,138,222,6,2+state->ticks%4,GOLD);
        rect(image,176,222,6,2+state->ticks%4,GOLD);
    }
    rect(image,9,207,52,23,PANEL);
    rect(image,273,207,38,23,PANEL);
    digits(image,17,211,state->speed,3,CYAN);
    digits(image,280,211,(TIME_LIMIT-time+999)/1000,2,time>TIME_LIMIT-7000?PINK:LIGHT);
    rect(image,9,234,302,3,PANEL);
    rect(image,9,234,state->distance*302/LENGTH,3,PINK);

    unsigned int first_row=first?0:HORIZON;
    doom_dcs_begin_native_rows(first_row);
    for (unsigned int y=first_row;y<H;++y) doom_dcs_write_native(y,lcd_row(image,y));
    doom_usb_video_capture_native_rows(lcd_row,preview_row,image,colors);
}

static void advance(struct race *state,uint32_t keys)
{
    int limit=(keys&KEY_DOWN)?43:(keys&KEY_ERASE)?176:113;
    if (abs(state->lane)>234 && limit>66) limit=66;
    state->speed+=clamp(limit-state->speed,-4,2);
    state->lane+=((keys&KEY_ENTER)!=0)*9-((keys&KEY_BACK)!=0)*9;
    state->lane+=(curve(state->distance+230)-curve(state->distance))*state->speed/4400;
    state->lane=clamp(state->lane,-335,335);
    state->distance+=1+state->speed/17;
    if (state->flash) --state->flash;
    for (unsigned int i=0;i<4;++i) {
        struct rival *rival=&state->traffic[i];
        rival->distance+=3+(i&1);
        int gap=rival->distance-state->distance;
        if (gap<220 && gap>115 && !rival->hit && abs(state->lane-rival->lane)<66) {
            rival->hit=true;
            state->speed/=2;
            state->flash=14;
            fixfinder_beep(32);
        }
        if (gap<70) {
            rival->distance=state->distance+2700+(i*213+state->ticks*7)%1000;
            rival->lane=(int)((state->ticks/11+i)%3)*146-146;
            rival->hit=false;
        }
    }
    ++state->ticks;
}

enum race_result fixfinder_race(const volatile uint32_t *clock)
{
    uint8_t *image=malloc(W*H);
    if (!image) return RACE_EXIT;
    struct race state={.speed=80,.traffic={{720,-146,false},{1480,0,false},{2300,146,false},{3100,-146,false}}};
    doom_display_set_palette(colors);
    skyline(image);
    uint32_t started=*clock, step=started, render=started;
    enum race_result result=RACE_EXIT;
    bool first=true;
    for (;;) {
        uint32_t now=*clock, elapsed=now-started;
        uint32_t keys=fixfinder_physical_keys(now), remote=doom_usb_input_buttons();
        uint32_t exit=(doom_usb_input_keys()&USB_INPUT_RETURN)?KEY_DOWN|KEY_BACK:keys;
        if (fixfinder_arcade_exit(exit,now)) break;
        if (state.distance>=LENGTH || elapsed>=TIME_LIMIT) {
            result=state.distance>=LENGTH?RACE_FINISHED:RACE_TIMEOUT;
            break;
        }
        if (remote&DOOM_BUTTON_LEFT) keys|=KEY_BACK;
        if (remote&DOOM_BUTTON_RIGHT) keys|=KEY_ENTER;
        if (remote&(DOOM_BUTTON_FORWARD|DOOM_BUTTON_FIRE)) keys|=KEY_ERASE;
        if (remote&DOOM_BUTTON_BACK) keys|=KEY_DOWN;
        while ((int32_t)(now-step)>=0) {
            advance(&state,keys);
            step+=16+(state.ticks&1U);
        }
        if ((int32_t)(now-render)>=0) {
            fixfinder_set_led(state.flash ? FIXFINDER_LED_RED :
                              elapsed >= TIME_LIMIT-7000 || abs(state.lane)>234 ?
                              FIXFINDER_LED_YELLOW : FIXFINDER_LED_GREEN);
            draw(image,&state,elapsed,first);
            first=false;
            render=now+32;
        }
    }
    free(image);
    return result;
}
