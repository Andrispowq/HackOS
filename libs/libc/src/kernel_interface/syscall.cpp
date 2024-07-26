#include "syscall.h"

#define DEFN_SYSCALL0(fn, num) uint64_t syscall_##fn() { return MakeSyscall(num, 0, 0, 0, 0, 0); }
#define DEFN_SYSCALL1(fn, num, P1) uint64_t syscall_##fn(P1 p1) { return MakeSyscall(num, (uint64_t)p1, 0, 0, 0, 0); }
#define DEFN_SYSCALL2(fn, num, P1, P2) uint64_t syscall_##fn(P1 p1, P2 p2) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, 0, 0, 0); }
#define DEFN_SYSCALL3(fn, num, P1, P2, P3) uint64_t syscall_##fn(P1 p1, P2 p2, P3 p3) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, 0, 0); }
#define DEFN_SYSCALL4(fn, num, P1, P2, P3, P4) uint64_t syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, (uint64_t)p4, 0); }
#define DEFN_SYSCALL5(fn, num, P1, P2, P3, P4, P5) uint64_t syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, (uint64_t)p4, (uint64_t)p5); }

DEFN_SYSCALL1(exit, 0, int)

DEFN_SYSCALL2(kprintf, 1, const char*, va_list)
DEFN_SYSCALL0(kprintf_backspace, 2)

DEFN_SYSCALL2(fopen, 3, const char*, int)
DEFN_SYSCALL4(fread, 4, void*, int, int, void*)
DEFN_SYSCALL4(fwrite, 5, void*, int, int, void*)
DEFN_SYSCALL1(fclose, 6, void*)

DEFN_SYSCALL3(kmalloc_int, 7, uint64_t, int, uint64_t*)
DEFN_SYSCALL1(kfree, 8, void*)

DEFN_SYSCALL0(kread, 9)

DEFN_SYSCALL3(create_window, 10, uint32_t, uint32_t, uint32_t) 
DEFN_SYSCALL1(destroy_window, 11, uint64_t)

DEFN_SYSCALL1(get_window_buffer, 12, uint64_t)
DEFN_SYSCALL2(set_window_title, 13, uint64_t, const char*)
