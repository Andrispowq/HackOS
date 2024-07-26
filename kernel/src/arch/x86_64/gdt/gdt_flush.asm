bits 64

global __gdt_flush
global __tss_flush

__gdt_flush:
    lgdt    [rdi]        ; Load the new GDT pointer

    mov     ax, 0x10      ; 0x10 is the offset in the GDT to our data segment
    mov     ds, ax        ; Load all data segment selectors
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax

    pop     rdi
    mov     rax, 0x08
    push    rax
    push    rdi
    retfq

__tss_flush:
    ltr     di
    ret