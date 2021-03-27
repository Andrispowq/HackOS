
[bits 16]

gdt_start_32:
    dd      0x0
    dd      0x0

gdt_code_32: 
    dw      0xFFFF
    dw      0x0
    db      0x0
    db      10011010b
    db      11001111b
    db      0x0

gdt_data_32:
    dw      0x0
    dw      0x0
    db      0x0
    db      10010010b
    db      11001111b
    db      0x0

gdt_end_32:

gdt_descriptor_32:
    dw gdt_end_32 - gdt_start_32 - 1
    dd gdt_start_32

CODE_SEG_32    equ gdt_code_32 - gdt_start_32
DATA_SEG_32    equ gdt_data_32 - gdt_start_32