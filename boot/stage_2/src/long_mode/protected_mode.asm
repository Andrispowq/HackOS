
%include "src/long_mode/gdt32.asm"

[bits 16]

extern BeginPM

EnablePM:
	cli ; 1. disable interrupts

    lgdt    [gdt_descriptor_32] ; 2. load the GDT descriptor
	
    mov     eax, cr0
    or      eax, 0x1 ; 3. set 32-bit mode bit in cr0
    mov     cr0, eax
	
    jmp     CODE_SEG_32:init_pm ; 4. far jump by using a different segment

[bits 32]

init_pm: ; we are now using 32-bit instructions
    mov     ax, DATA_SEG_32 ; 5. update the segment registers
    mov     ds, ax
    mov     ss, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax

    mov     ebp, 0x90000 ; 6. update the stack right at the top of the free space
    mov     esp, ebp

    call    BeginPM ; 7. Call a well-known label with useful code