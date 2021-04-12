[bits 16]

; ESI -> start cluster of the file
LoadFile:
	; Lets load the fuck out of this file
	; Step 1. Setup buffer
	mov 	bx, 0x0000
	mov 	es, bx
	mov 	bx, 0x0500

	; Load 
    .loop:
		; Clustertime
		call 	ReadCluster

		; Check
		cmp 	esi, 0x0FFFFFF8
		jb 		.loop

	; Done, jump
	mov 	dl, byte [FAT32_PhysicalDriveNum]
	mov 	dh, 4
	jmp 	0x0:KernelOffset

	; Safety catch
	cli
	hlt

; ES:DI -> buffer, SI -> cluster number; ESI -> next cluster in chain
ReadCluster:
	pusha

    push    bx

	; Calculate Sector
	; FirstSectorofCluster = ((N – 2) * BPB_SecPerClus) + FirstDataSector;
	xor 	eax, eax
	xor 	bx, bx
	xor 	ecx, ecx
	mov 	ax, si
	sub 	ax, 2
	mov 	bl, byte [FAT32_SectorsPerCluster]
	mul 	bx
	add 	eax, dword [FAT32_Reserved0]

	; EAX is now the sector of data, should be EBX
    pop     bx
	mov 	cl, byte [FAT32_SectorsPerCluster]

	call 	ReadDisk

	; Save position
	mov 	word [FAT32_Reserved2], bx
	push 	es

	; SI still has cluster num, call next
	call 	GetNextCluster
	mov 	dword [FAT32_Reserved1], esi

	; Restore
	pop 	es

	; Done
	popa
    
    mov 	bx, word [FAT32_Reserved2]
	mov 	esi, dword [FAT32_Reserved1]

	ret

; SI -> cluster num; ESI -> next cluster num
GetNextCluster:
	; Calculte Sector in FAT
	xor 	eax, eax
	xor 	edx, edx
	mov 	ax, si
	shl 	ax, 2 			; REM * 4, since entries are 32 bits long, and not 8
	div 	word [FAT32_BytesPerSector]
	add 	ax, word [FAT32_ReservedSectors]
	push 	dx

	; AX contains sector
	; DX contains remainder
	mov 	ecx, 1
	mov 	bx, 0x0000
	mov 	es, bx
	mov 	bx, 0x7E00
	push 	es
	push 	bx

	; Read Sector
	call 	ReadDisk
	pop 	bx
	pop 	es

	; Find Entry
	pop 	dx
	xchg 	si, dx
	mov 	esi, dword [es:bx + si]
	ret