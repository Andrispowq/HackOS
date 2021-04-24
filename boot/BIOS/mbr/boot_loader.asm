[org 0x0600]
[bits 16]

global _start

_start:
    cli

    xor     ax, ax
    mov     ss, ax
    mov     sp, ax
    mov     ds, ax
    mov     es, ax

    mov     cx, 0x0100            ; 256 WORDs in MBR
    mov     si, 0x7C00            ; Current MBR Address
    mov     di, 0x0600            ; New MBR Address
    rep     movsw                 ; Copy MBR

    jmp     0x0:safe_start

safe_start:
	mov     [boot_drive], dl ; Remember that the BIOS sets us the boot drive in 'dl' on boot
    mov     bp, sp
    
    sti 

    mov     bx, WelcomeString
    call    Print
    call    PrintHex
    
    call    PrintLn

    mov     bx, partition_1
    mov     cx, 4 ; there are 4 partitions

loop:
    mov     al, byte [bx]
    test    al, 0x80
    jnz     found
    add     bx, 0x10
    dec     cx
    jnz     loop
    jmp     no_active_part

found:
    mov     word [partition_off], bx 
    add     bx, 8

    mov     ebx, dword [bx] ; LBA start
    mov     di, 0x7C00
    mov     cx, 1
    call    ReadDisk

    cmp     word [0x7DFE], 0xAA55 ; Check Boot Signature
    jne     not_bootable
    mov     si, word [partition_off] 
    mov     dl, byte [boot_drive]
    jmp     0x0:0x7C00 ; Jump To VBR

no_active_part:    
    mov     bx, NoActivePartitionString
    call    Print
    jmp     $

not_bootable:
    mov     bx, NotBootablePartitionString
    call    Print
    jmp     $

%include "print.asm"
%include "disk_reader.asm"

WelcomeString: db "MBR loaded! Active disk: ", 0x00
NoActivePartitionString: db "No active partition!", 0x00
NotBootablePartitionString: db "Active partition is not bootable!", 0x00

boot_drive:     db 0
partition_off:  db 0

times 0x1B4 - ($ - $$) nop

UID: times 10   db 0

partition_1: 
    db 0x80 ; Active partition
    db 0x00 ; CHS start
    db 0x00
    db 0x00
    db 0x00 ; Partition type
    db 0x00 ; CHS end
    db 0x00
    db 0x00
    dd 0x01 ; LBA start
    dd 333 ; LBA size, 1 + 64 + 12 + 256 = 333 

partition_2: 
    db 0x00 ; Not active partition
    db 0x00 ; CHS start
    db 0x00
    db 0x00
    db 0x0B ; Partition type
    db 0x00 ; CHS end
    db 0x00
    db 0x00
    dd 334 ; LBA start
    dd 93750 ; LBA size, the size of our HackOS_FAT.img

partition_3: times 16 db 0
partition_4: times 16 db 0

boot_signature: dw 0xAA55