/* File-backed bonus games. FatFs allocates clusters inside the existing FAT32
 * filesystem; no partition, format, or raw placement operation is used. */
#include "game_library.h"
#include "arcade.h"
#include "display.h"
#include "usb_video.h"
#include "../lcd/fixfinder_io.h"
#include "storage/ff.h"
#include "chip8/octo_emulator.h"

enum { MAX_GAMES=16, W=DOOM_FRAME_WIDTH, H=DOOM_FRAME_HEIGHT };
static const char file_name[]="0:/FFARCADE.PAK";
static FATFS fs __attribute__((section(".arcade_art")));
struct pack_entry {
    char title[16];
    uint32_t offset, length, crc;
    uint16_t tickrate;
    uint8_t quirks, reserved, keys[4];
};
_Static_assert(sizeof(struct pack_entry)==36, "pack entry layout");
static struct pack_entry list[MAX_GAMES] __attribute__((section(".arcade_art")));
static unsigned int count;
extern void fixfinder_library_status(unsigned int stage, unsigned int count);
extern int fixfinder_library_write_allowed(void);
extern void fixfinder_library_enable_writes(bool enable);
extern void doom_dcs_present(const uint8_t *frame);

static bool read_exact(FIL *f, void *buffer, unsigned int size)
{
    UINT got=0;
    return f_read(f,buffer,size,&got)==FR_OK && got==size;
}

#ifdef FIXFINDER_PROVISION_LIBRARY
extern const uint8_t fixfinder_pack_start[],fixfinder_pack_end[];
static bool provision(void)
{
    static const char temp[]="0:/FFARCADE.TMP";
    if (!fixfinder_library_write_allowed()) return false;
    FIL f;
    UINT written=0;
    unsigned int size=(uintptr_t)fixfinder_pack_end-(uintptr_t)fixfinder_pack_start;
    fixfinder_library_enable_writes(true);
    bool ok=false;
    if (f_open(&f,temp,FA_WRITE|FA_CREATE_NEW)!=FR_OK) goto done;
    ok=f_write(&f,fixfinder_pack_start,size,&written)==FR_OK && written==size;
    if (f_close(&f)!=FR_OK) ok=false;
    if (ok && f_open(&f,temp,FA_READ)==FR_OK) {
        uint8_t bytes[256];
        for (unsigned int off=0;off<size && ok;) {
            unsigned int n=size-off<sizeof bytes?size-off:sizeof bytes;
            ok=read_exact(&f,bytes,n) && !memcmp(bytes,fixfinder_pack_start+off,n);
            off+=n;
        }
        if (f_close(&f)!=FR_OK) ok=false;
    } else ok=false;
    if (ok) ok=f_rename(temp,file_name)==FR_OK;
done:
    fixfinder_library_enable_writes(false);
    return ok;
}
#endif

unsigned int fixfinder_library_init(void)
{
    count=0;
    FRESULT rc=f_mount(&fs,"0:",1);
    if (rc!=FR_OK || fs.fs_type!=FS_FAT32) {
        fixfinder_library_status(0x100u+rc,0);
        return 0;
    }
    FIL f;
    rc=f_open(&f,file_name,FA_READ);
#ifdef FIXFINDER_PROVISION_LIBRARY
    if (rc==FR_NO_FILE && provision()) rc=f_open(&f,file_name,FA_READ);
#endif
    if (rc!=FR_OK) {
        fixfinder_library_status(0x200u+rc,0);
        return 0;
    }
    struct { char magic[8]; uint32_t version, count; } header;
    bool ok=read_exact(&f,&header,sizeof header) && !memcmp(header.magic,"FFARCADE",8)
        && header.version==1 && header.count && header.count<=MAX_GAMES
        && read_exact(&f,list,header.count*sizeof *list);
    uint32_t end=sizeof header+(ok?header.count*sizeof *list:0);
    for (unsigned int i=0;ok && i<header.count;++i) {
        const struct pack_entry *e=&list[i];
        ok=e->title[0] && e->title[15]==0 && e->offset>=end && e->offset<=f_size(&f)
            && e->length>=2 && e->length<=3584 && e->length<=f_size(&f)-e->offset
            && e->tickrate && e->tickrate<=1000 && !(e->quirks&~63u);
        for (const char *s=e->title;ok && *s;++s)
            ok=(*s>='A'&&*s<='Z') || (*s>='0'&&*s<='9') || *s==' ';
        for (unsigned int k=0;k<4;++k) ok=ok && e->keys[k]<16;
        end=e->offset+e->length;
    }
    f_close(&f);
    if (ok) count=header.count;
    fixfinder_library_status(ok?1u:0x300u,count);
    return count;
}

const char *fixfinder_library_title(unsigned int index)
{
    return index<count?list[index].title:"";
}

static uint32_t crc32(const uint8_t *p,unsigned int n)
{
    uint32_t c=~0u;
    while (n--) {
        c^=*p++;
        for (unsigned int i=0;i<8;++i) c=(c>>1)^((c&1)?0xedb88320u:0);
    }
    return ~c;
}

bool fixfinder_library_play(unsigned int index,uint8_t *frame,const volatile uint32_t *clock)
{
    if (index>=count) return false;
    const struct pack_entry *e=&list[index];
    uint8_t rom[3584];
    FIL f;
    if (f_open(&f,file_name,FA_READ)!=FR_OK) return false;
    bool ok=f_lseek(&f,e->offset)==FR_OK && read_exact(&f,rom,e->length)
        && crc32(rom,e->length)==e->crc;
    f_close(&f);
    if (!ok) return false;
    octo_emulator *vm=malloc(sizeof *vm);
    if (!vm) return false;
    octo_options opts;
    octo_default_options(&opts);
    opts.q_shift=!!(e->quirks&1);
    opts.q_loadstore=!!(e->quirks&2);
    opts.q_jump0=!!(e->quirks&4);
    opts.q_logic=!!(e->quirks&8);
    opts.q_clip=!!(e->quirks&16);
    opts.q_vblank=!!(e->quirks&32);
    octo_emulator_init(vm,(char *)rom,e->length,&opts,NULL);
    static const uint8_t rgb[768]={8,13,22, 146,241,163, 237,101,99, 255,219,115};
    doom_display_set_palette(rgb);
    uint32_t previous=15, deadline=*clock;
    unsigned int ticks=0;
    while (!vm->halt) {
        uint32_t now=*clock, keys=fixfinder_keys(now);
        if (fixfinder_arcade_exit(keys,now)) break;
        memset(vm->keys,0,sizeof vm->keys);
        if (keys!=(KEY_DOWN|KEY_BACK)) {
            for (unsigned int i=0;i<4;++i) if (keys&(1u<<i)) {
                vm->keys[e->keys[i]]=1;
                if (vm->wait && !(previous&(1u<<i))) {
                    vm->v[(unsigned int)vm->wait_reg]=e->keys[i];
                    vm->wait=0;
                }
            }
        }
        previous=keys;
        for (unsigned int i=0;i<e->tickrate && !vm->halt && !vm->wait;++i) {
            bool drawing=(vm->ram[vm->pc]>>4)==0xd;
            octo_emulator_instruction(vm);
            if (drawing && opts.q_vblank) break;
        }
        if (vm->dt) --vm->dt;
        if (vm->st && (--vm->st&3u)==0) fixfinder_beep(20);
        if (!(ticks&1u)) {
            memset(frame,0,W*H);
            unsigned int w=vm->hires?128:64, h=vm->hires?64:32;
            for (unsigned int y=0;y<104;++y)
                for (unsigned int x=0;x<208;++x)
                    frame[(y+28)*W+x+16]=vm->px[(y*h/104)*w+x*w/208];
            doom_dcs_present(frame);
            doom_usb_video_capture(frame,rgb);
        }
        deadline+=ticks++%3==0?16:17;
        if ((int32_t)(*clock-deadline)>50) deadline=*clock;
        while ((int32_t)(*clock-deadline)<0) {}
    }
    free(vm);
    return true;
}
