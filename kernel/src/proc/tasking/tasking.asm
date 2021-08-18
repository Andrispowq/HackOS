[bits 64]

global StartProcess
global StartProcess_FirstTime

%macro PUSHALL 0
    push    0
    push    0

    push    r15
    push    r14
    push    r13
    push    r12
    push    r11
    push    r10
    push    r9
    push    r8

    push    rax
    push    rcx
    push    rdx
    push    rbx
    push    rsp
    push    rbp
    push    rsi
    push    rdi

    sub     rsp, 256
    movdqa  [rsp + 15 * 16], xmm15
    movdqa  [rsp + 14 * 16], xmm14
    movdqa  [rsp + 13 * 16], xmm13
    movdqa  [rsp + 12 * 16], xmm12
    movdqa  [rsp + 11 * 16], xmm11
    movdqa  [rsp + 10 * 16], xmm10
    movdqa  [rsp + 9 * 16], xmm9
    movdqa  [rsp + 8 * 16], xmm8
    movdqa  [rsp + 7 * 16], xmm7
    movdqa  [rsp + 6 * 16], xmm6
    movdqa  [rsp + 5 * 16], xmm5
    movdqa  [rsp + 4 * 16], xmm4
    movdqa  [rsp + 3 * 16], xmm3
    movdqa  [rsp + 2 * 16], xmm2
    movdqa  [rsp + 1 * 16], xmm1
    movdqa  [rsp + 0 * 16], xmm0
%endmacro

%macro POPALL 0
    movdqa  xmm15, [rsp + 15 * 16]
    movdqa  xmm14, [rsp + 14 * 16]
    movdqa  xmm13, [rsp + 13 * 16]
    movdqa  xmm12, [rsp + 12 * 16]
    movdqa  xmm12, [rsp + 11 * 16]
    movdqa  xmm10, [rsp + 10 * 16]
    movdqa  xmm9, [rsp + 9 * 16]
    movdqa  xmm8, [rsp + 8 * 16]
    movdqa  xmm7, [rsp + 7 * 16]
    movdqa  xmm6, [rsp + 6 * 16]
    movdqa  xmm5, [rsp + 5 * 16]
    movdqa  xmm4, [rsp + 4 * 16]
    movdqa  xmm3, [rsp + 3 * 16]
    movdqa  xmm2, [rsp + 2 * 16]
    movdqa  xmm1, [rsp + 1 * 16]
    movdqa  xmm0, [rsp + 0 * 16]
    add     rsp, 256

    pop     rdi
    pop     rsi
    pop     rbp
    add     rsp, 8 ; skip rsp
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax

    pop     r8
    pop     r9
    pop     r10
    pop     r11
    pop     r12
    pop     r13
    pop     r14
    pop     r15

    add     rsp, 0x10
%endmacro

StartProcess:
    mov     rsp, rbp ;skip Schedule
    pop     rbp     
    mov     rsp, rbp ;skip timer_callback
    pop     rbp
    mov     rsp, rbp ;skip ISRHandler
    pop     rbp
    pop     rax      ;skip return address

    add     rsp, 0x28 ;skip offset added in isr_common_stub

    mov     [rsi], rsp ;save the stack top

    ;should now be at the context set at the interrupt handler, load the new stack, and pop everything off
    mov     cr3, rdx
    mov     rsp, rdi

    POPALL

    sti
    iretq

StartProcess_FirstTime:
    mov     cr3, rsi
    mov     rsp, rdi

    POPALL

    sti
    iretq