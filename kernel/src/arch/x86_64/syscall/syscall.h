#ifndef SYSCALL_H
#define SYSCALL_H

#include "lib/stdint.h"
#include "lib/string.h"
#include "lib/memory.h"

extern "C" int MakeSyscall(uint64_t syscall, uint64_t p0, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4);
extern "C" int ExecuteSyscall(uint64_t p0, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4, uint64_t addr);

void InitialiseSyscalls();

#define DECL_SYSCALL0(fn) int syscall_##fn();
#define DECL_SYSCALL1(fn,p1) int syscall_##fn(p1);
#define DECL_SYSCALL2(fn,p1,p2) int syscall_##fn(p1,p2);
#define DECL_SYSCALL3(fn,p1,p2,p3) int syscall_##fn(p1,p2,p3);
#define DECL_SYSCALL4(fn,p1,p2,p3,p4) int syscall_##fn(p1,p2,p3,p4);
#define DECL_SYSCALL5(fn,p1,p2,p3,p4,p5) int syscall_##fn(p1,p2,p3,p4,p5);

DECL_SYSCALL1(kprintf, const char*)
DECL_SYSCALL0(kprintf_backspace)

#endif