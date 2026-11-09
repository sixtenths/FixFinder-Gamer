ENTRY(Reset_Handler)

MEMORY {
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 3072K
    RAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 192K
    TCM (rw)   : ORIGIN = 0x10000000, LENGTH = 64K
}

_sp = ORIGIN(RAM) + LENGTH(RAM);
__heap_end = _sp - 12K;

SECTIONS {
    .vectors : { KEEP(*(.vectors)) KEEP(*(.isr_vector)) } > FLASH
    .text : { *(.text*) *(.rodata*) } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH

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
}

ASSERT((_sidata & 3) == 0, "Startup requires aligned data")
ASSERT(_ebss + 148K <= __heap_end, "Insufficient room for Doom zone and libc heap")
