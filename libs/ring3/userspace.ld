OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)
PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}
SECTIONS
{
    . = 0x0000400000000000;
    .text : ALIGN(4096) { *(.text .text.*) } :text
    .rodata : ALIGN(4096) { *(.rodata .rodata.* .lrodata .lrodata.*) } :rodata
    .data : ALIGN(4096) { *(.data .data.* .ldata .ldata.*) } :data
    .bss : ALIGN(4096) { *(COMMON) *(.bss .bss.* .lbss .lbss.*) } :data
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
