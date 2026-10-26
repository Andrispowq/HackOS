[bits 64]

global __strlen
global __strcpy


__strlen:
    push    rbx
    push    rcx

    mov     rbx, rdi
    xor     al, al
    mov     rcx, 0xffffffff

    cld
    repne   scasb
    sub     rdi, rbx
    mov     rax, rdi
    dec     rax

    pop     rcx
    pop     rbx

    ret

__strcpy:
    mov     rax, rdi
.copy:
    mov     cl, [rsi]
    mov     [rdi], cl
    inc     rsi
    inc     rdi
    test    cl, cl
    jnz     .copy
    ret
