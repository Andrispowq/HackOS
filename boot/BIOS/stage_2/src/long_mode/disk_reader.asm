[bits 16]

DAPACK: db	0x10
	    db	0
blkcnt:	dw	64
db_add:	dw	0x8000
	    dw	0
d_lba:	dd	0x1
	    dd	0

%include "src/long_mode/print_rm.asm"

global ReadDisk

; cx -> read size, eax -> LBA addr, es:bx -> buffer
ReadDisk:
    pusha

    mov     word [blkcnt], cx
    mov     word [db_add], bx
    mov     word [db_add + 2], es
    mov     dword [d_lba], eax

	mov     si, DAPACK		; address of "disk address packet"
	mov     ah, 0x42		; AL is unused
	int     0x13
	jc      disk_error

    popa
    ret

ext_not_supp:
    mov     bx, ExtNotSupportedString
    call    PrintRM
    call    PrintLn

    jmp     $

disk_error:
    mov     bx, DiskErrorString
    call    PrintRM
    call    PrintLn

    mov     dh, ah
    call    PrintHex

    jmp     $

ExtNotSupportedString: db "INT 13H LBA extension not supported!", 0
DiskErrorString: db "Disk read error!", 0