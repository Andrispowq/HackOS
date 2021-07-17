[bits 64]

global JumpToAddress

JumpToAddress:
    cli

    mov     rax, rsi
    mov     rbp, rdx
    mov     rsp, rcx
    mov     cr3, rax
    mov     rax, 0x12345

    sti
    jmp     rdi