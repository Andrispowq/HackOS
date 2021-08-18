[bits 64]

extern _entry

global _start

_start:
    and     rsp, 0xFFFFFFFFFFFFFFF0
    call    _entry
    ud2

end: