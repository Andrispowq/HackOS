bits 64
global _start
extern main
extern MakeSyscall
section .text
_start:
    xor ebp, ebp
    and rsp, -16
    ; RDI=argc, RSI=argv from the loader; alignment preserves both.
    call main
    mov esi, eax
    xor edi, edi ; exit retains syscall number zero
    call MakeSyscall
    ud2
section .note.GNU-stack noalloc noexec nowrite progbits
