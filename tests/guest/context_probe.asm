bits 64
global context_probe
section .text
context_probe:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 1048 ; two aligned FXSAVE areas and scratch
    fxsave64 [rsp]
    mov word [rsp+1024], 0x077f
    fldcw [rsp+1024]
    mov dword [rsp+1028], 0x3f80
    ldmxcsr [rsp+1028]
    fld1
%assign i 0
%rep 16
    pcmpeqd xmm%+i, xmm%+i
%assign i i+1
%endrep
    mov rax, 0x11
    mov rbx, 0x22
    mov rbp, 0x33
    mov rdx, 0x44
    mov rdi, 0x55
    mov rsi, 0x66
    mov r8, 0x88
    mov r9, 0x99
    mov r10, 0xaa
    mov r11, 0xbb
    mov r12, 0xcc
    mov r13, 0xdd
    mov r14, 0xee
    mov r15, 0xff
    mov rcx, 100000000
    std
    stc
.spin:
    dec rcx
    jnz .spin
    jnc .flags_fail
    pushfq
    pop rcx
    test rcx, 0x400
    jz .flags_fail
    cld
%macro CHECK_REG 2
    cmp %1, %2
    jne .fail
%endmacro
    CHECK_REG rax, 0x11
    CHECK_REG rbx, 0x22
    CHECK_REG rbp, 0x33
    CHECK_REG rdx, 0x44
    CHECK_REG rdi, 0x55
    CHECK_REG rsi, 0x66
    CHECK_REG r8, 0x88
    CHECK_REG r9, 0x99
    CHECK_REG r10, 0xaa
    CHECK_REG r11, 0xbb
    CHECK_REG r12, 0xcc
    CHECK_REG r13, 0xdd
    CHECK_REG r14, 0xee
    CHECK_REG r15, 0xff
    fxsave64 [rsp+512]
    cmp word [rsp+512], 0x077f
    jne .fail
    cmp dword [rsp+512+24], 0x3f80
    jne .fail
    fstp qword [rsp+1032]
    mov rax, 0x3ff0000000000000
    cmp [rsp+1032], rax
    jne .fail
    mov rcx, 32
.xmm:
    cmp qword [rsp+512+160+rcx*8-8], -1
    jne .fail
    loop .xmm
    xor eax, eax
    jmp .done
.flags_fail:
    cld
.fail:
    mov eax, 1
.done:
    fxrstor64 [rsp]
    add rsp, 1048
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
section .note.GNU-stack noalloc noexec nowrite progbits
