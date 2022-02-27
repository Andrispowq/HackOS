#include "syscall.h"

#include "arch/x86_64/interrupts/idt.h"
#include "proc/tasking/process.h"
#include "lib/stdio.h"

#define DEFN_SYSCALL0(fn, num) uint64_t syscall_##fn() { return MakeSyscall(num, 0, 0, 0, 0, 0); }
#define DEFN_SYSCALL1(fn, num, P1) uint64_t syscall_##fn(P1 p1) { return MakeSyscall(num, (uint64_t)p1, 0, 0, 0, 0); }
#define DEFN_SYSCALL2(fn, num, P1, P2) uint64_t syscall_##fn(P1 p1, P2 p2) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, 0, 0, 0); }
#define DEFN_SYSCALL3(fn, num, P1, P2, P3) uint64_t syscall_##fn(P1 p1, P2 p2, P3 p3) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, 0, 0); }
#define DEFN_SYSCALL4(fn, num, P1, P2, P3, P4) uint64_t syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, (uint64_t)p4, 0); }
#define DEFN_SYSCALL5(fn, num, P1, P2, P3, P4, P5) uint64_t syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, (uint64_t)p4, (uint64_t)p5); }

DEFN_SYSCALL1(exit, 0, int)

DEFN_SYSCALL1(kprintf, 1, const char*)
DEFN_SYSCALL0(kprintf_backspace, 2)

DEFN_SYSCALL2(fopen, 3, const char*, int)
DEFN_SYSCALL4(fread, 4, void*, int, int, void*)
DEFN_SYSCALL4(fwrite, 5, void*, int, int, void*)
DEFN_SYSCALL1(fclose, 6, void*)

void exit(int code)
{
    _Kill();
}

static void* syscalls[7] =
{
    (void*)&exit,

    (void*)&kprintf,
    (void*)&kprintf_backspace,

    (void*)&fopen,
    (void*)&fread,
    (void*)&fwrite,
    (void*)&fclose
};

uint64_t num_syscalls = 7;

static void syscall_handler(Registers* registers)
{
    if(registers->rax >= num_syscalls) //For now lets call this kprintf
    {
        return;
    }

    void* function = syscalls[registers->rax];

    uint64_t ret = ExecuteSyscall(registers->rdi, registers->rsi, registers->rdx, registers->rcx, registers->rbx, (uint64_t)function);    
    registers->rax = ret;
}

void InitialiseSyscalls()
{
    RegisterInterruptHandler(0x80, syscall_handler);
}