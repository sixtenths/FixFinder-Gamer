#include <stdint.h>
volatile uint16_t scanline[240];
volatile uint32_t rendered_row;
static void Halt_Handler(void){for(;;)__asm volatile("wfi");}
void Reset_Handler(void);
__attribute__((used,section(".isr_vector"))) const uintptr_t vectors[128]={[0]=0x20020000u,[1]=(uintptr_t)Reset_Handler,[2 ... 127]=(uintptr_t)Halt_Handler};
void Reset_Handler(void){__asm volatile("cpsid i");for(;;){for(uint32_t y=0;y<320;y++){uint16_t color=y<106?0xF800u:y<213?0x07E0u:0x001Fu;for(uint32_t x=0;x<240;x++)scanline[x]=color;rendered_row=y;}}}
