
[bits 64]

extern ISRHandler
extern IRQHandler

global __set_idt

__set_idt:
    mov     rax, rdi
    lidt    [rax]
    ret

%macro PUSHALL 0
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

    xor     rax, rax
    mov     ax, ds              ; Lower 16-bits of rax = ds.
    push    rax                 ; save the data segment descriptor

    mov     ax, 0x10            ; load the kernel data segment descriptor
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
%endmacro

%macro POPALL 0
    pop     rbx                      ; reload the original data segment descriptor
    mov     ds, bx
    mov     es, bx
    mov     fs, bx
    mov     gs, bx

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

	; Cleanup irq & error code from stack
    add     rsp, 0x10
%endmacro

isr_common_stub:
	PUSHALL

    mov     rdi, rsp
    sub     rsp, 0x28

    cld
	call    ISRHandler
    add     rsp, 0x28

    POPALL

    sti
	iretq

irq_common_stub:
    PUSHALL

    mov     rdi, rsp
    sub     rsp, 0x28

    cld
    call    IRQHandler
    add     rsp, 0x28

	POPALL

    sti
	iretq

; We don't get information about which interrupt was caller
; when the handler is run, so we will need to have a different handler
; for every interrupt.
; Furthermore, some interrupts push an error code onto the stack but others
; don't, so we will push a dummy error code for those which don't, so that
; we have a consistent stack for all of them.

; First make the ISRs global
global isr0
global isr1
global isr2
global isr3
global isr4
global isr5
global isr6
global isr7
global isr8
global isr9
global isr10
global isr11
global isr12
global isr13
global isr14
global isr15
global isr16
global isr17
global isr18
global isr19
global isr20
global isr21
global isr22
global isr23
global isr24
global isr25
global isr26
global isr27
global isr28
global isr29
global isr30
global isr31

; IRQs
global irq0
global irq1
global irq2
global irq3
global irq4
global irq5
global irq6
global irq7
global irq8
global irq9
global irq10
global irq11
global irq12
global irq13
global irq14
global irq15

global isr128

%macro ISR_NOERR 1
isr%1:
    cli
    push    0
    push    %1
    jmp     isr_common_stub
%endmacro

%macro ISR_ERR 1
isr%1:
    cli
    push    %1
    jmp     isr_common_stub
%endmacro

%macro IRQ 1
irq%1:
    cli
	push    0
	push    (%1 + 32)
	jmp     irq_common_stub
%endmacro

; 0: Divide By Zero Exception
ISR_NOERR 0

; 1: Debug Exception
ISR_NOERR 1

; 2: Non Maskable Interrupt Exception
ISR_NOERR 2

; 3: Int 3 Exception
ISR_NOERR 3

; 4: INTO Exception
ISR_NOERR 4

; 5: Out of Bounds Exception
ISR_NOERR 5

; 6: Invalid Opcode Exception
ISR_NOERR 6

; 7: Coprocessor Not Available Exception
ISR_NOERR 7

; 8: Double Fault Exception (With Error Code!)
ISR_ERR 8

; 9: Coprocessor Segment Overrun Exception
ISR_NOERR 9

; 10: Bad TSS Exception (With Error Code!)
ISR_ERR 10

; 11: Segment Not Present Exception (With Error Code!)
ISR_ERR 11

; 12: Stack Fault Exception (With Error Code!)
ISR_ERR 12

; 13: General Protection Fault Exception (With Error Code!)
ISR_ERR 13

; 14: Page Fault Exception (With Error Code!)
ISR_ERR 14

; 15: Reserved Exception
ISR_ERR 15

; 16: Floating Point Exception
ISR_ERR 16

; 17: Alignment Check Exception
ISR_ERR 17

; 18: Machine Check Exception
ISR_ERR 18

; 19: Reserved
ISR_ERR 19

; 20: Reserved
ISR_ERR 20

; 21: Reserved
ISR_ERR 21

; 22: Reserved
ISR_ERR 22

; 23: Reserved
ISR_ERR 23

; 24: Reserved
ISR_ERR 24

; 25: Reserved
ISR_ERR 25

; 26: Reserved
ISR_ERR 26

; 27: Reserved
ISR_ERR 27

; 28: Reserved
ISR_ERR 28

; 29: Reserved
ISR_ERR 29

; 30: Reserved
ISR_ERR 30

; 31: Reserved
ISR_ERR 31

; IRQ handlers
IRQ 0
IRQ 1
IRQ 2
IRQ 3
IRQ 4
IRQ 5
IRQ 6
IRQ 7
IRQ 8
IRQ 9
IRQ 10
IRQ 11
IRQ 12
IRQ 13
IRQ 14
IRQ 15

; 128: aka. int 0x80, the syscall handler
ISR_NOERR 128