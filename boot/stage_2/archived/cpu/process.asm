bits 32

global copy_page_physical
global jump_to_ecx

copy_page_physical:
    push    ebx
    pushf

    cli

    mov     ebx, [esp + 12]   ; Source address
    mov     ecx, [esp + 16]   ; Destination address

    ; Disable paging
    mov     edx, cr0
    and     edx, 0x7fffffff
    mov     cr0, edx

    mov     edx, 1024

.loop:
    mov     eax, [ebx]        ; Get the word at the source address
    mov     [ecx], eax        ; Store it at the dest address
    add     ebx, 4            ; Source address += sizeof(word)
    add     ecx, 4            ; Dest address += sizeof(word)
    dec     edx               ; One less word to do
    jnz     .loop

    ; Enable paging
    mov     edx, cr0
    or      edx, 0x80000000
    mov     cr0, edx

    popf
    pop     ebx
    
    ret

jump_to_ecx:
    cli
    
    mov     ecx, [esp + 0x4]
    mov     eax, [esp + 0x8]
    mov     ebp, [esp + 0xC]
    mov     esp, [esp + 0x10]
    mov     cr3, eax
    mov     eax, 0x12345

    sti
    jmp     ecx