[org 0x7C00]
[bits 16]

global _start

_start:
	mov     [BootDrive], dl ; Remember that the BIOS sets us the boot drive in 'dl' on boot
	mov     sp, 0x7C00 ; set the stack
    mov     bp, sp

    call    EnableA20

    mov     bx, BootLoaderString
    call    Print

    mov     bx, 0x02
    mov     di, KernelOffset
    mov     cx, 64
    mov     dl, [BootDrive]
    call    ReadDisk

    call    KernelOffset
    jmp     $

%include "print.asm"
%include "disk_reader.asm"
%include "a20_gate.asm"

KernelOffset:   equ 0x8000
BootDrive:     db 0

BootLoaderString: db "HackOS bootloader loaded!", 0x00

times 510 - ($ - $$) nop
dw 0xAA55