[bits 64]

global MakeSyscall
global ExecuteSyscall
global SetupSysret
global JumpToUserspace

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

ExecuteSyscall:
    mov     rax, r9
    call    rax
    ret

SetupSysret:
	mov	    rcx, 0xC0000080 ; IA32_EFER
	rdmsr
	or      rax, 1 ; set SCE (syscall extensions)
	wrmsr
	mov	    rcx, 0xC0000081 ; STAR
	rdmsr
	mov	    rdx, 0x00180008 ; syscall base is 0x08, sysret base is 0x18
	wrmsr
	ret

JumpToUserspace:
    mov     ax, 0x23      ; 0x20 is the offset in the GDT to our data segment
    mov     ds, ax        ; Load all data segment selectors
    mov     es, ax
    mov     fs, ax
    mov     gs, ax

    mov     rcx, rdi ; set the new rip
    mov     rsp, rsi ; set the new stack
    mov     r11, 0x202 ; rflags, enable intrerrupts
    o64     sysret ; to userspace and beyond