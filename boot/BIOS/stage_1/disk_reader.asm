[bits 16]

DAPACK: db	0x10
	    db	0
blkcnt:	dw	64
db_add:	dw	0x8000
	    dw	0
d_lba:	dd	0x1
	    dd	0

; cx -> read size, eax -> LBA addr, es:bx -> buffer
ReadDisk:
    pusha

    mov     word [blkcnt], cx
    mov     word [db_add], bx
    mov     dword [d_lba], eax

    mov     ah, 0x41 ; LBA extensions
    mov     bx, 0x55AA
    int     0x13 
    jc      ext_not_supp

	mov     si, DAPACK		; address of "disk address packet"
	mov     ah, 0x42		; AL is unused
	int     0x13
	jc      disk_error

    popa
    ret

ext_not_supp:
    mov     bx, ExtNotSupportedString
    call    Print
    call    PrintLn

    jmp     $

disk_error:
    mov     bx, DiskErrorString
    call    Print
    call    PrintLn

    mov     dh, ah
    call    PrintHex

    jmp     $

ExtNotSupportedString: db "INT 13H LBA extension not supported!", 0
DiskErrorString: db "Disk read error!", 0