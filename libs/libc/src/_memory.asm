[bits 64]

global __memset
global __memcpy
global __memcmp

__memset:
    mov     rcx, rdx
    mov     rax, rsi
    rep     stosb
    ret

__memcpy:
    mov     rcx, rdx
    rep     movsb
    ret

__memcmp:
    xor     rax, rax
    mov     rcx, rdx

    cld 
    repe    cmpsb
    
    setz    al
    ret