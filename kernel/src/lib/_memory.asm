[bits 64]
global __memset
global __memcpy
global __memcmp

__memset:
    mov     r8, rdi
    mov     rcx, rdx
    mov     eax, esi
    cld
    rep     stosb
    mov     rax, r8
    ret

__memcpy:
    mov     rax, rdi
    mov     rcx, rdx
    cld
    rep     movsb
    ret

__memcmp:
    xor     ecx, ecx
    test    rdx, rdx
    jz      .equal
.compare:
    movzx   eax, byte [rdi + rcx]
    movzx   r8d, byte [rsi + rcx]
    sub     eax, r8d
    jnz     .done
    inc     rcx
    cmp     rcx, rdx
    jb      .compare
.equal:
    xor     eax, eax
.done:
    ret
