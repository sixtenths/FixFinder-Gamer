ENTRY(Reset_Handler)
MEMORY { FLASH(rx) : ORIGIN = 0x08004000, LENGTH = 512K
RAM(rwx) : ORIGIN = 0x20000000, LENGTH = 128K }
_sp = ORIGIN(RAM) + LENGTH(RAM);
SECTIONS {
.vectors : { KEEP(*(.vectors)) KEEP(*(.isr_vector)) } > FLASH
.text : { *(.text*) *(.rodata*) } > FLASH
.ARM.exidx : { *(.ARM.exidx*) } > FLASH
_sidata = LOADADDR(.data);
.data : { _sdata = .; *(.data*) _edata = .; } > RAM AT> FLASH
.bss (NOLOAD) : { _sbss = .; *(.bss*) *(COMMON) _ebss = .; } > RAM
}
