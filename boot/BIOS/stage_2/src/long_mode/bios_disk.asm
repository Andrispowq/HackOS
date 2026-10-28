; The bridge lives below 64 KiB. All C code and its stack are identity mapped.
section .must_be_top
[bits 64]
global BiosReadSector
global BiosBootDrive
BiosReadSector:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [saved_rsp], rsp
    mov [packet_lba], edi
    sgdt [saved_gdt]
    sidt [saved_idt]
    fxsave [saved_fx]
    lgdt [bridge_gdt_ptr]
    push qword 0x18
    lea rax, [compat]
    push rax
    retfq
[bits 32]
compat:
    mov eax, cr0
    and eax, 0x7fffffff
    mov cr0, eax
    mov ecx, 0xc0000080
    rdmsr
    and eax, ~(1 << 8)
    wrmsr
    jmp 0x28:pm16
[bits 16]
pm16:
    mov ax, 0x30
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov eax, cr0
    and eax, ~1
    mov cr0, eax
    jmp 0:real
real:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    lidt [real_idt]
    mov word [packet_count], 1
    mov si, packet
    mov dl, [BiosBootDrive]
    mov ah, 0x42
    sti
    int 0x13
    cli
    setnc byte [cs:disk_ok]
    xor ax, ax
    mov ds, ax
    lgdt [bridge_gdt_ptr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x18:back32
[bits 32]
back32:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov ecx, 0xc0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax
    jmp 0x08:back64
[bits 64]
back64:
    lgdt [saved_gdt]
    lidt [saved_idt]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, [saved_rsp]
    fxrstor [saved_fx]
    cld
    movzx eax, byte [disk_ok]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
align 8
bridge_gdt:
    dq 0
    dq 0x00af9a000000ffff ; 64-bit code
    dq 0x00cf92000000ffff ; data
    dq 0x00cf9a000000ffff ; 32-bit code
    dq 0x00cf92000000ffff ; 32-bit data
    dq 0x00009a000000ffff ; 16-bit code
    dq 0x000092000000ffff ; 16-bit data
bridge_gdt_ptr:
    dw 55
    dq bridge_gdt
real_idt:
    dw 1023
    dd 0
saved_gdt: times 10 db 0
saved_idt: times 10 db 0
saved_rsp: dq 0
BiosBootDrive: db 0
disk_ok: db 0
align 16
packet:
    db 16, 0
packet_count: dw 1
    dw 0, 0x2000 ; one-sector bounce buffer at 0x20000
packet_lba: dq 0
align 16
saved_fx: times 512 db 0
