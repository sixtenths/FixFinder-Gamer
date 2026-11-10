/* GD32F427 FixFinder Rev C. Read-only eMMC identification and sector reads. */
#include <stdint.h>
#define R(a) (*(volatile uint32_t *)(a))
#define S(o) R(0x40012c00u+(o))
#define G(p,o) R(0x40020000u+((p)*0x400u)+(o))
enum { PWR=0, CLK=4, ARG=8, CMD=12, RESP=20, DTIME=36, LEN=40, DATA=44, STAT=52, CLEAR=56, FIFO=128 };
struct result { uint32_t magic,stage,stat,ocr,cid[4],csd[4],sectors,revision,partition,words;
    uint32_t ext[128],sector0[128],sector1[128],sector_last[128];
    uint32_t library_stage, library_count, pacman_verified; };
volatile struct result fixfinder_emmc_result __attribute__((section(".arcade_art")));
#define out fixfinder_emmc_result

static void pin(unsigned int p,unsigned int n,unsigned int pull) {
    unsigned int s=n*2, shift=(n&7)*4, af=n<8?32:36;
    G(p,af)=(G(p,af)&~(15u<<shift))|(12u<<shift);
    G(p,4)&=~(1u<<n);
    G(p,8)=(G(p,8)&~(3u<<s))|(1u<<s);
    G(p,12)=(G(p,12)&~(3u<<s))|(pull<<s);
    G(p,0)=(G(p,0)&~(3u<<s))|(2u<<s);
}
static void delay(unsigned int n) { while(n--) __asm volatile("nop"); }
static int cmd(unsigned int n,uint32_t arg,unsigned int response) {
    S(CLEAR)=0x7ff;
    S(ARG)=arg;
    S(CMD)=(1u<<10)|(response<<6)|n;
    for (unsigned int i=0;i<2000000;i++) {
        unsigned int s=S(STAT);
        if(s&0xc5) {
            out.stat=s;
            return !(s&4) && (!(s&1) || n==1);
        }
    }
    out.stat=S(STAT);
    return 0;
}
static int read_block_once(unsigned int command,uint32_t arg,volatile uint32_t *dest) {
    S(DATA)=0; S(CLEAR)=0x7ff;
    for (unsigned int i=0;i<128 && (S(STAT)&(1u<<21));++i) (void)S(FIFO);
    S(DTIME)=2400000; S(LEN)=512; S(DATA)=(9u<<4)|3;
    if (!cmd(command,arg,1)) { S(DATA)=0; return 0; }
    unsigned int count=0,s=0;
    for(unsigned int i=0;i<8000000;i++) {
        s=S(STAT);
        if(s&((1u<<1)|(1u<<3)|(1u<<5)|(1u<<9))) break;
        if((s&(1u<<21)) && count<128) dest[count++]=S(FIFO);
        if((s&(1u<<8)) && count==128) break;
    }
    S(DATA)=0;
    out.words=count; out.stat=s;
    return count==128 && (s&(1u<<8)) && !(s&0x22a);
}
static int block(unsigned int command,uint32_t arg,volatile uint32_t *dest) {
    for (unsigned int attempt=0;attempt<3;++attempt) {
        if (read_block_once(command,arg,dest)) return 1;
        delay(200000);
    }
    return 0;
}
void fixfinder_emmc_probe(void) {
    volatile uint32_t *p=(volatile uint32_t *)&out;
    for(unsigned int i=0;i<sizeof(out)/4;i++) p[i]=0;
    out.magic=0x454d4d43;
    R(0x40023830)|=(1u<<2)|(1u<<3); /* GPIOC/D */
    R(0x40023844)|=1u<<11;
    (void)R(0x40023844);
    R(0x40023824)|=1u<<11; R(0x40023824)&=~(1u<<11);
    pin(2,8,1); pin(2,12,0); pin(3,2,1);
    S(PWR)=3; S(CLK)=(1u<<8)|238; /* 200 kHz from 48 MHz */
    delay(300000);
    out.stage=1;
    if(!cmd(0,0,0)) goto done;
    out.stage=2;
    for(unsigned int i=0;i<500;i++) {
        if(!cmd(1,0x40ff8000,1)) goto done;
        out.ocr=S(RESP);
        if(out.ocr>>31) break;
        delay(100000);
    }
    if((out.ocr&0xc0000000)!=0xc0000000) goto done;
    out.stage=3;
    if(!cmd(2,0,3)) goto done;
    for(unsigned int i=0;i<4;i++) out.cid[i]=S(RESP+4*i);
    out.stage=4;
    if(!cmd(3,1u<<16,1)) goto done;
    out.stage=5;
    if(!cmd(9,1u<<16,3)) goto done;
    for(unsigned int i=0;i<4;i++) out.csd[i]=S(RESP+4*i);
    out.stage=6;
    if(!cmd(7,1u<<16,1)) goto done;
    delay(300000);
    out.stage=7;
    if(!block(8,0,out.ext)) goto done;
    out.sectors=out.ext[53]; /* EXT_CSD[212..215] */
    out.revision=out.ext[48]&255;
    out.partition=(out.ext[44]>>24)&255; /* EXT_CSD[179] */
    out.stage=8;
    if((out.partition&7) || !out.sectors || !block(17,0,out.sector0)) goto done;
    out.stage=9;
    if(!block(17,1,out.sector1)) goto done;
    out.stage=10;
    if(!block(17,out.sectors-1,out.sector_last)) goto done;
    out.stage=11;
done:
    S(CLK)=0;
    out.stage|=0x80000000u;
}

void fixfinder_emmc_snapshot(uint8_t *dest)
{
    const volatile uint8_t *src = (const volatile uint8_t *)&out;
    for (unsigned int i = 0; i < sizeof(out); ++i) dest[i] = src[i];
}

#ifdef FIXFINDER_EMMC_LIBRARY
#include "storage/diskio.h"
#include "gd32f4xx.h"
#include <stdbool.h>
static bool writes_enabled;

void fixfinder_library_enable_writes(bool enable)
{
    writes_enabled=enable;
}

void fixfinder_library_status(unsigned int stage, unsigned int count)
{
    out.library_stage=stage;
    out.library_count=count;
}

void fixfinder_library_pacman_status(bool verified)
{
    out.pacman_verified=verified;
}

int fixfinder_library_write_allowed(void)
{
    return out.stage==0x8000000bu && out.cid[0]==0x7001004du &&
           out.cid[1]==0x36323730u && out.cid[2]==0x34021762u && out.cid[3]==0x5210a953u &&
           R(0x1fff7a10)==0x3532409eu && R(0x1fff7a14)==0x0a383439u &&
           R(0x1fff7a18)==0x32313457u;
}

DSTATUS disk_status(BYTE drive)
{
    return drive || out.stage!=0x8000000bu?STA_NOINIT:0;
}

DSTATUS disk_initialize(BYTE drive)
{
    DSTATUS status=disk_status(drive);
    if (!status) S(CLK)=(1u<<8)|14u; /* 3 MHz, 1-bit bus */
    return status;
}

DRESULT disk_read(BYTE drive,BYTE *dst,DWORD sector,UINT count)
{
    if (disk_status(drive)) return RES_NOTRDY;
    if (!count || sector>=out.sectors || count>out.sectors-sector) return RES_PARERR;
    uint32_t tmp[128];
    while (count--) {
        if (!block(17,sector++,tmp)) return RES_ERROR;
        for (unsigned int i=0;i<512;++i) dst[i]=(uint8_t)(tmp[i/4]>>(8*(i&3)));
        dst+=512;
    }
    return RES_OK;
}

static int card_ready(void)
{
    for (unsigned int i=0;i<1200;++i) {
        if (!cmd(13,1u<<16,1)) return 0;
        uint32_t r=S(RESP);
        if (r&0xfdffa000u) return 0;
        if ((r&0x1f00u)==0x0900u) return 1;
        delay(10000);
    }
    return 0;
}

static bool write_block(DWORD sector, const BYTE *src)
{
    if (!card_ready()) return false;
    /* The polled transmit FIFO must be serviced until the block leaves SDIO.
     * Keep USB and display work out of this short data phase. */
    uint32_t irq = __get_PRIMASK();
    __disable_irq();
    bool sent = cmd(24, sector, 1) && !(S(RESP) & 0xfdffa000u);
    unsigned int words = 0, status = 0;
    if (sent) {
        S(DATA) = 0; S(CLEAR) = 0x7ff;
        S(DTIME) = 2400000; S(LEN) = 512; S(DATA) = (9u << 4) | 1u;
        for (unsigned int i = 0; i < 8000000u; ++i) {
            status = S(STAT);
            if (status & 0x21au) break;
            if ((status & (1u << 14)) && words < 128) {
                for (unsigned int n = 0; n < 8; ++n, ++words, src += 4)
                    S(FIFO) = (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
                              ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
            }
            if ((status & (1u << 8)) && words == 128) break;
        }
        S(DATA) = 0;
    }
    __set_PRIMASK(irq);
    return sent && words == 128 && (status & (1u << 8)) && !(status & 0x21au) && card_ready();
}

DRESULT disk_write(BYTE drive,const BYTE *src,DWORD sector,UINT count)
{
    if (disk_status(drive)) return RES_NOTRDY;
    if (!writes_enabled || !fixfinder_library_write_allowed()) return RES_WRPRT;
    if (!count || sector>=out.sectors || count>out.sectors-sector) return RES_PARERR;
    while (count--) {
        uint32_t clock = S(CLK);
        bool written = false;
        for (unsigned int attempt = 0; attempt < 3 && !written; ++attempt) {
            if (attempt) S(CLK) = (clock & ~255U) | ((16U << attempt) - 2U);
            written = write_block(sector, src);
            if (!written) delay(200000);
        }
        if (S(CLK) != clock) S(CLK) = clock;
        if (!written) return RES_ERROR;
        ++sector;
        src += 512;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE drive,BYTE request,void *value)
{
    if (disk_status(drive)) return RES_NOTRDY;
    if (request==CTRL_SYNC) return card_ready()?RES_OK:RES_ERROR;
    if (request==GET_SECTOR_COUNT) { *(DWORD *)value=out.sectors; return RES_OK; }
    if (request==GET_SECTOR_SIZE) { *(WORD *)value=512; return RES_OK; }
    return RES_PARERR;
}
#endif
