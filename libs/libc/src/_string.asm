[bits 64]

global __strlen
global __strcmp
global __strncmp
global __strcpy

extern __memcpy

__strlen:
    push    rbx
    push    rcx

    mov     rbx, rdi
    xor     al, al
    mov     rcx, 0xffffffff

    repne   scasb
    sub     rdi, rbx
    mov     rax, rdi
    dec     rax

    pop     rcx
    pop     rbx

    ret

; rcx -> length1; rdx -> length2
__strcmp:
    push    rdi
    push    rsi

    call    __strlen
    mov     rcx, rax

    mov     rdi, rsi
    call    __strlen
    mov     rdx, rax

    pop     rsi
    pop     rdi

    xor     rax, rax

    cmp     rcx, rdx
    je      compare
    mov     rax, rdx
    sub     rax, rcx
    ret

compare:
    cld 
    repe    cmpsb

    setz    al
    ret

__strncmp:
    xor     rax, rax
    mov     rcx, rdx

    cld 
    repe    cmpsb
    
    setz    al
    ret

__strcpy:
    push    rdi

    mov     rdi, rsi
    call    __strlen
    mov     rdx, rax
    inc     rdx ; Copy the \0 as well

    pop     rdi

    call    __memcpy
    mov     rax, rdi
    ret    
