
[bits 32]

gdt_start_64:
    dw      0xFFFF
    dw      0x0
    db      0x0
    db      0x0
    db      0x1
    db      0x0

gdt_code_64: 
    dw      0x0
    dw      0x0
    db      0x0
    db      10011010b
    db      10101111b
    db      0x0

gdt_data_64:
    dw      0x0
    dw      0x0
    db      0x0
    db      10010010b
    db      00000000b
    db      0x0

gdt_end_64:

gdt_descriptor_64:
    dw gdt_end_64 - gdt_start_64 - 1
    dd gdt_start_64

CODE_SEG_64    equ gdt_code_64 - gdt_start_64
DATA_SEG_64    equ gdt_data_64 - gdt_start_64