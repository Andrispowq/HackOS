#include "syscall.h"

#define DEFN_SYSCALL0(fn, num) uint64_t syscall_##fn() { return MakeSyscall(num, 0, 0, 0, 0, 0); }
#define DEFN_SYSCALL1(fn, num, P1) uint64_t syscall_##fn(P1 p1) { return MakeSyscall(num, (uint64_t)p1, 0, 0, 0, 0); }
#define DEFN_SYSCALL2(fn, num, P1, P2) uint64_t syscall_##fn(P1 p1, P2 p2) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, 0, 0, 0); }
#define DEFN_SYSCALL3(fn, num, P1, P2, P3) uint64_t syscall_##fn(P1 p1, P2 p2, P3 p3) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, 0, 0); }
#define DEFN_SYSCALL4(fn, num, P1, P2, P3, P4) uint64_t syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, (uint64_t)p4, 0); }
#define DEFN_SYSCALL5(fn, num, P1, P2, P3, P4, P5) uint64_t syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, (uint64_t)p4, (uint64_t)p5); }

DEFN_SYSCALL1(kprintf, 0, const char*)
DEFN_SYSCALL0(kprintf_backspace, 1)

DEFN_SYSCALL2(fopen, 2, const char*, int)
DEFN_SYSCALL4(fread, 3, void*, int, int, void*)
DEFN_SYSCALL4(fwrite, 4, void*, int, int, void*)
DEFN_SYSCALL1(fclose, 5, void*)
