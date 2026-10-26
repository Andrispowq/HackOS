bits 64
global syscall_test_target
section .text
syscall_test_target:
    mov rax, rsp
    and rax, 15
    cmp rax, 8
    jne .fail
    cmp rdi, 11
    jne .fail
    cmp rsi, 22
    jne .fail
    cmp rdx, 33
    jne .fail
    cmp rcx, 44
    jne .fail
    cmp r8, 55
    jne .fail
    mov eax, 123
    ret
.fail:
    xor eax, eax
    ret
%ifidn __OUTPUT_FORMAT__, elf64
section .note.GNU-stack noalloc noexec nowrite progbits
%endif
