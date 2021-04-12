[org 0x7C00]
[bits 16]

global _start

; Jump across the BPB
jmp short _start
nop

; The BIOS parameter block
FAT32_OSName				db		"Hack  OS"
FAT32_BytesPerSector		dw		0
FAT32_SectorsPerCluster		db		0
FAT32_ReservedSectors		dw		0
FAT32_NumFATs				db		0
FAT32_RootEntries			dw		0
FAT32_TotalSectors			dw		0
FAT32_MediaType				db		0
FAT32_SectorsPerFat			dw		0
FAT32_SectorsPerTrack		dw		0
FAT32_HeadsPerCylinder		dw		0
FAT32_HiddenSectors			dd 		0
FAT32_LargeTotalSectors		dd 		0

; The extension block
FAT32_SectorsPerFAT32		dd 		0
FAT32_Flags					dw		0
FAT32_Version				dw		0
FAT32_RootDirStart			dd 		0
FAT32_FSInfoSector			dw		0
FAT32_BackupBootSector		dw		0

; Reserved 
FAT32_Reserved0				dd		0 	;FirstDataSector
FAT32_Reserved1				dd		0 	;ReadCluster
FAT32_Reserved2				dd 		0 	;ReadCluster

FAT32_PhysicalDriveNum		db		0
FAT32_Reserved3				db		0
FAT32_BootSignature			db		0
FAT32_VolumeSerial			dd 		0
FAT32_VolumeLabel			db		"HACKOS PART" ; 11 bytes
FAT32_FSName				db		"FAT32   " ; 8 bytes

_start:
    cli

    xor     ax, ax
    mov     ss, ax
    mov     sp, ax
    mov     ds, ax
    mov     es, ax

    jmp     0x0:safe_start

safe_start:
	mov     [BootDrive], dl ; Remember that the BIOS sets us the boot drive in 'dl' on boot
	mov     sp, 0x7C00 ; set the stack
    mov     bp, sp
    
    sti

    call    EnableA20

    mov     bx, BootLoaderString
    call    Print
    call    PrintHex
    call    PrintLn    

    mov     ax, 0x02
    mov     bx, KernelOffset
    mov     cx, 64
    mov     dl, [BootDrive]
    call    ReadDisk

    jmp     0x0:KernelOffset
    jmp     $

%include "print.asm"
%include "disk_reader.asm"
%include "a20_gate.asm"

KernelOffset:   equ 0x8000
BootDrive:     db 0

BootLoaderString: db "HackOS bootloader loaded! Disk: ", 0x00

times 510 - ($ - $$) nop
dw 0xAA55