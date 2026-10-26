; A debugger-runner self-test. These names imitate the kernel milestones;
; this payload does not initialize a kernel or deliver an IRQ.
[bits 16]
section .text
global kernel_main
global _Z13InitialiseIRQv
global timer_callback
kernel_main:
    nop
%ifdef HANG
    jmp $
%endif
    jmp _Z13InitialiseIRQv
_Z13InitialiseIRQv:
    nop
    jmp timer_callback
timer_callback:
    cli
.halt:
    hlt
    jmp .halt
