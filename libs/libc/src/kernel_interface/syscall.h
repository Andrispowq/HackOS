#ifndef SYSCALL_H
#define SYSCALL_H

#include "stdint.h"
#include <stdarg.h>

extern "C" uint64_t MakeSyscall(uint64_t syscall, uint64_t p0, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4);

#define DECL_SYSCALL0(fn) uint64_t syscall_##fn();
#define DECL_SYSCALL1(fn,p1) uint64_t syscall_##fn(p1);
#define DECL_SYSCALL2(fn,p1,p2) uint64_t syscall_##fn(p1,p2);
#define DECL_SYSCALL3(fn,p1,p2,p3) uint64_t syscall_##fn(p1,p2,p3);
#define DECL_SYSCALL4(fn,p1,p2,p3,p4) uint64_t syscall_##fn(p1,p2,p3,p4);
#define DECL_SYSCALL5(fn,p1,p2,p3,p4,p5) uint64_t syscall_##fn(p1,p2,p3,p4,p5);

DECL_SYSCALL1(exit, int) //exit code

DECL_SYSCALL2(kprintf, const char*, va_list) //format, args
DECL_SYSCALL0(kprintf_backspace)

DECL_SYSCALL2(fopen, const char*, int) //file name, flags
DECL_SYSCALL4(fread, void*, int, int, void*) //pointer, size, n, file
DECL_SYSCALL4(fwrite, void*, int, int, void*) //pointer, size, n , file
DECL_SYSCALL1(fclose, void*) //file

DECL_SYSCALL3(kmalloc_int, uint64_t, int, uint64_t*) //size, align, address
DECL_SYSCALL1(kfree, void*) //address

DECL_SYSCALL0(kread)

DECL_SYSCALL3(create_window, uint32_t, uint32_t, uint32_t) //width, height, flags
DECL_SYSCALL1(destroy_window, uint64_t) //windowID

DECL_SYSCALL1(get_window_buffer, uint64_t) //windowID
DECL_SYSCALL2(set_window_title, uint64_t, const char*) //windowID, title

#endif