[bits 16]
extern MemoryRegionCount

DetectMemory:
    mov byte [MemoryRegionCount], 0
    mov ax, 0x3000
    mov es, ax
    xor di, di
    xor ebx, ebx
.next:
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, 24
    mov dword [es:di + 20], 1
    int 0x15
    jc .failed
    cmp eax, 0x534d4150
    jne .failed
    cmp ecx, 20
    jb .failed
    test dword [es:di + 20], 1
    jz .skip
    inc byte [MemoryRegionCount]
    test ebx, ebx
    jz .done
    cmp byte [MemoryRegionCount], 255
    je .failed
    add di, 24
    jmp .next
.skip:
    test ebx, ebx
    jnz .next
    jmp .done
.failed:
    mov byte [MemoryRegionCount], 0
.done:
    xor ax, ax
    mov es, ax
    ret
