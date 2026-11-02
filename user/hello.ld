OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
SECTIONS {
    . = 0x400080;
    .text : { *(.text.start) *(.text .text.*) *(.rodata .rodata.*) }
    /DISCARD/ : { *(.eh_frame*) *(.note*) *(.comment) }
}
