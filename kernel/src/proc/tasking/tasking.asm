[bits 64]

global JumpToAddress

JumpToAddress:
    cli

    mov     rbp, rdx
    mov     rsp, rcx
    mov     cr3, rsi
    mov     rax, 0x12345

    sti
    jmp     rdi