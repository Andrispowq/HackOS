[bits 64]

global MakeSyscall

MakeSyscall:
    push    rbp
    mov     rbp, rsp

    mov     rax, rdi
    mov     rdi, rsi
    mov     rsi, rdx
    mov     rdx, rcx
    mov     rcx, r8
    mov     r8, r9
    int     0x80

    pop     rbp
    ret