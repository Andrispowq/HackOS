[bits 16]

DAPACK: db	0x10
	    db	0
blkcnt:	dw	64
db_add:	dw	0x8000
	    dw	0
d_lba:	dd	0x1
	    dd	0

switch_endian_word:
    mov     bh, al
    mov     bl, ah
    ret

; cx -> read size, ebx -> LBA addr, es:di -> buffer
ReadDisk:
    pusha

    mov     word [blkcnt], cx
    mov     word [db_add], di
    mov     dword [d_lba], ebx

    mov     ah, 0x41 ; LBA extensions
    mov     bx, 0x55AA
    or      dl, 0x80
    int     0x13 
    jc      ext_not_supp

	mov     si, DAPACK		; address of "disk address packet"
	mov     ah, 0x42		; AL is unused
	or      dl, 0x80		; drive number (OR the drive # with 0x80)
	int     0x13
	jc      disk_error

    popa
    ret

ext_not_supp:
    mov     bx, EXT_NOT_SUPPORTED
    call    Print
    call    PrintLn

    jmp     $

disk_error:
    mov     bx, DISK_ERROR
    call    Print
    call    PrintLn

    mov     dh, ah
    call    PrintHex

    jmp     $

EXT_NOT_SUPPORTED: db "INT 13 LBA ext not supported!", 0
DISK_ERROR: db "Disk read error!", 0