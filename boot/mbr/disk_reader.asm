[bits 16]

; cx -> read size, ebx -> LBA addr, es:di -> buffer
ReadDisk:
    pusha
    push    dx

    mov     ah, 0x08 ; Get sectors per track
    int     0x13

    inc     dh
    mov     [NumberOfHeads], dh
    and     cl, 0x3F
    mov     [SectorsPerTrack], cl

    mov     ax, dx
    push    ax

    mov     eax, [SectorsPerTrack]
    div     ebx
    inc     edx
    mov     cl, dl ; sector

    mov     eax, [NumberOfHeads]
    div     ebx
    mov     dh, dl ; head
    mov     ch, bl ; cylinder

    pop     ax
    mov     dl, al

    mov     ah, 0x02 ; Read function
    int     0x13
    jc      disk_error 

    pop     dx
    cmp     al, dh 
    jne     sectors_error
    popa
    ret

disk_error:
    mov     bx, DISK_ERROR
    call    Print
    call    PrintLn

    mov     dh, ah
    call    PrintHex

    jmp     disk_loop

sectors_error:
    mov     bx, SECTORS_ERROR
    call    Print

disk_loop:
    jmp $

DISK_ERROR: db "Disk read error!", 0
SECTORS_ERROR: db "Incorrect number of sectors read!", 0

NumberOfHeads: db 0
SectorsPerTrack: db 0