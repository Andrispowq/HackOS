[org 0x7C00]
[bits 16]

global _start

_start:
	mov     [BOOT_DRIVE], dl ; Remember that the BIOS sets us the boot drive in 'dl' on boot
	mov     sp, 0x7C00 ; set the stack
    mov     bp, sp

    call    EnableA20

    mov     bx, REAL_MODE
    call    Print

    mov     bx, KERNEL_OFFSET ; Read from disk and store in 0x8000
    mov     dh, 64 ; The kernel may be bigger later!
    mov     dl, [BOOT_DRIVE]
    call    ReadDisk

    call    KERNEL_OFFSET
    jmp     $

%include "print.asm"
%include "disk_reader.asm"
%include "a20_gate.asm"

KERNEL_OFFSET   equ 0x8000

BOOT_DRIVE:     db 0
REAL_MODE:      db "Started in real mode!", 0x00
VIDEO_ERROR:    db "ERROR while setting the video mode!", 0x00

times 510 - ($ - $$) db 0
dw 0xAA55