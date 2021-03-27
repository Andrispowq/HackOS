[bits 32]

; this is how constants are defined
VIDEO_MEMORY equ 0xB8000
WHITE_ON_BLACK equ 0x0F ; the color byte for each character

Print:
    pusha
    mov     edx, VIDEO_MEMORY

print_pm_loop:
    mov     al, [ebx] ; [ebx] is the address of our character
    mov     ah, WHITE_ON_BLACK

    cmp     al, 0 ; check if end of string
    je      print_pm_done

    mov     [edx], ax ; store character + attribute in video memory
    add     ebx, 1 ; next char
    add     edx, 2 ; next video memory position

    jmp     print_pm_loop

print_pm_done:
    popa
    ret