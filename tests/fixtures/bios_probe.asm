; Test payload only: the real MBR and stage 1 must load this from LBA 2 at 0x8000.
[org 0x8000]
[bits 16]
mov dx, 0xf4
mov ax, 0x10
out dx, ax
cli
.halt: hlt
jmp .halt
