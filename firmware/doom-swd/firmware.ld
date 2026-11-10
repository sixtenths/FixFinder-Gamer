ENTRY(Reset_Handler)

MEMORY {
    FLASH (rx) : ORIGIN = DEFINED(__fixfinder_app_base) ? __fixfinder_app_base : 0x08000000,
                 LENGTH = (DEFINED(__fixfinder_map_slot) ? __fixfinder_map_slot : 0x08300000) - ORIGIN(FLASH)
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 192K
    TCM (rw)   : ORIGIN = 0x10000000, LENGTH = 64K
}

_sp = ORIGIN(RAM) + LENGTH(RAM) - (DEFINED(__fixfinder_app_base) ? 8 : 0);
__heap_end = _sp - 12K;

SECTIONS {
    .vectors : { KEEP(*(.vectors)) KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
    .ARM.exidx : {
        PROVIDE(__exidx_start = .);
        *(.ARM.exidx*)
        PROVIDE(__exidx_end = .);
    } > FLASH

    .data : {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> FLASH
    _sidata = LOADADDR(.data);

    .bss (NOLOAD) : {
        _sbss = .;
        *(.bss*) *(COMMON)
        . = ALIGN(8);
        _ebss = .;
    } > RAM

    .doom_frame (NOLOAD) : { *(.doom_frame) } > TCM
    .doom_setup (NOLOAD) : { *(.doom_setup) } > TCM
    .doom_video (NOLOAD) : { *(.doom_video) } > TCM
    .arcade_art (NOLOAD) : { *(.arcade_art) } > TCM
    .arcade_visual (NOLOAD) : {
        . = ALIGN(8);
        __doom_line_scratch = .;
        *(.arcade_visual)
        . = DEFINED(__fixfinder_map_slot) ? MAX(., __doom_line_scratch + 12K) : .;
        __doom_line_scratch_end = .;
    } > TCM
}

ASSERT((_sidata & 3) == 0, "Startup requires aligned data")
ASSERT(_ebss + (DEFINED(__fixfinder_minimum_heap) ? __fixfinder_minimum_heap : 148K) <= __heap_end,
       "Insufficient room for the requested heap")
