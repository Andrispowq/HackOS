[bits 64]

global ExecuteSyscall

ExecuteSyscall:
    mov     rax, r9
    sub     rsp, 8 ; align before the SysV call
    call    rax
    add     rsp, 8
    ret
