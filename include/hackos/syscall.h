#pragma once
#include "hackos/types.h"
#include "hackos/syscall_numbers.h"
#include <stdarg.h>

// int 0x80: RAX=number; RDI, RSI, RDX, RCX, R8=arguments; RAX=result.
// Raw pointers and va_list are retained for compatibility, not a ring-3 ABI.
extern "C" uint64_t MakeSyscall(uint64_t number, uint64_t p0, uint64_t p1,
                                uint64_t p2, uint64_t p3, uint64_t p4);

#define DECL_SYSCALL0(fn) uint64_t syscall_##fn();
#define DECL_SYSCALL1(fn,p1) uint64_t syscall_##fn(p1);
#define DECL_SYSCALL2(fn,p1,p2) uint64_t syscall_##fn(p1,p2);
#define DECL_SYSCALL3(fn,p1,p2,p3) uint64_t syscall_##fn(p1,p2,p3);
#define DECL_SYSCALL4(fn,p1,p2,p3,p4) uint64_t syscall_##fn(p1,p2,p3,p4);
#define DECL_SYSCALL5(fn,p1,p2,p3,p4,p5) uint64_t syscall_##fn(p1,p2,p3,p4,p5);

#define HACKOS_SYSCALL(number, name, target, arity, ...) DECL_SYSCALL##arity(name, ##__VA_ARGS__)
#include "hackos/syscalls.def"
#undef HACKOS_SYSCALL
#undef DECL_SYSCALL0
#undef DECL_SYSCALL1
#undef DECL_SYSCALL2
#undef DECL_SYSCALL3
#undef DECL_SYSCALL4
#undef DECL_SYSCALL5
