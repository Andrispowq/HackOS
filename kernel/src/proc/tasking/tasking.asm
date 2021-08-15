[bits 64]

global StartProcess

StartProcess:
    cli 

    mov     cr3, rdi
    mov     rsp, rsi

    pop     rdi
    pop     rsi
    pop     rbp
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

    pop     rax

    sti
    jmp     rax