[bits 16]

extern MemoryRegionCount

DetectMemory:
	mov 	byte [MemoryRegionCount], 0
	mov     ax, 0
	mov     es, ax
	mov     di, 0x5000
	mov     edx, 0x534D4150
	xor     ebx, ebx

repeat:
	mov     eax, 0xE820
	mov     ecx, 24
	int     0x15

	add     di, 24
	inc     byte [MemoryRegionCount]

	cmp     ebx, 0
	je      finished

	jmp     repeat

finished:
	ret