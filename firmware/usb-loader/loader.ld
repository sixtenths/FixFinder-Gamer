ENTRY(Reset_Handler)

MEMORY {
    CODE (rx) : ORIGIN = DEFINED(__ram_test) ? 0x20010000 : 0x08000000, LENGTH = 16K
    RAM (rwx) : ORIGIN = DEFINED(__ram_test) ? 0x20014000 : 0x20000000, LENGTH = 48K
}
_sp = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS {
    .vectors : { KEEP(*(.vectors)) } > CODE
    .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > CODE
    .ARM.exidx : { *(.ARM.exidx*) } > CODE
    .data : {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT> CODE
    _sidata = LOADADDR(.data);
    .bss (NOLOAD) : {
        _sbss = .;
        *(.bss*) *(COMMON)
        . = ALIGN(8);
        _ebss = .;
    } > RAM
}
ASSERT((_sidata & 3) == 0, "Startup requires aligned data")
ASSERT(_ebss + 4K <= _sp, "Insufficient loader stack space")
