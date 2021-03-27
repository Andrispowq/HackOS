
[bits 16]

extern loader_main

section .must_be_top

global _start
global BeginPM
global MemoryRegionCount

_start:
	jmp 	_start_text

section .text

_start_text:
    mov     ax, 1920
    mov     bx, 1080
    mov     cl, 32

    call    vbe_set_mode
	jc 		vbe_set
	jmp 	$

vbe_set:
	call 	DetectMemory
	call 	EnablePM

%include "src/long_mode/video_modes.asm"
%include "src/long_mode/detect_memory.asm"

section .text

%include "src/long_mode/protected_mode.asm"
%include "src/long_mode/print.asm"
%include "src/long_mode/cpuid.asm"
%include "src/long_mode/paging.asm"

[bits 32]

%include "src/long_mode/gdt64.asm"

BeginPM:
	call 	DetectCPUID
	call 	DetectLongMode
	call 	SetupIdentityPaging

	lgdt 	[gdt_descriptor_64]

	jmp 	CODE_SEG_64:Start64Bit

[bits 64]

Start64Bit:
	cli

    mov     ax, DATA_SEG_64
    mov     ds, ax
    mov     ss, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax

	call 	ActivateSSE

	mov 	rsp, default_stack_pointer
	mov 	rbp, rsp
	
	mov 	rdi, vbe_final_info_block
	call 	loader_main

	jmp 	$

ActivateSSE:
	mov rax, cr0
	and ax, 0b11111101
	or ax, 0b00000001
	mov cr0, rax

	mov rax, cr4
	or ax, 0b1100000000
	mov cr4, rax

	ret

PROT_MODE: db "Landed in protected mode!", 0x00
LONG_MODE: db "Landed in long mode!", 0x00
VIDEO_ERROR: db "ERROR: video mode not supported", 0x00

MemoryRegionCount: db 0

KERNEL_STACK_SIZE   equ 8192 

section .bss
align 4
resb KERNEL_STACK_SIZE
default_stack_pointer: