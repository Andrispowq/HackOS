#include "syscall.h"
#include "arch/x86_64/interrupts/idt.h"

#include "lib/stdio.h"

#define DEFN_SYSCALL0(fn, num) int syscall_##fn() { return MakeSyscall(num, 0, 0, 0, 0, 0); }
#define DEFN_SYSCALL1(fn, num, P1) int syscall_##fn(P1 p1) { return MakeSyscall(num, (uint64_t)p1, 0, 0, 0, 0); }
#define DEFN_SYSCALL2(fn, num, P1, P2) int syscall_##fn(P1 p1, P2 p2) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, 0, 0, 0); }
#define DEFN_SYSCALL3(fn, num, P1, P2, P3) int syscall_##fn(P1 p1, P2 p2, P3 p3) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, 0, 0); }
#define DEFN_SYSCALL4(fn, num, P1, P2, P3, P4) int syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, (uint64_t)p4, 0); }
#define DEFN_SYSCALL5(fn, num, P1, P2, P3, P4, P5) int syscall_##fn(P1 p1, P2 p2, P3 p3, P4 p4, P5 p5) { return MakeSyscall(num, (uint64_t)p1, (uint64_t)p2, (uint64_t)p3, (uint64_t)p4, (uint64_t)p5); }

DEFN_SYSCALL1(kprintf, 0, const char*);
DEFN_SYSCALL0(kprintf_backspace, 1);

static void* syscalls[3] =
{
    (void*)&kprintf,
    (void*)&kprintf_backspace
};

uint64_t num_syscalls = 2;

static void syscall_handler(Registers* registers)
{
    if(registers->rax >= num_syscalls) //For now lets call this kprintf
    {
        return;
    }

    void* function = syscalls[registers->rax];

    int ret = ExecuteSyscall(registers->rdi, registers->rsi, registers->rdx, registers->rcx, registers->rbx, (uint64_t)function);    
    registers->rax = ret;
}

void InitialiseSyscalls()
{
    RegisterInterruptHandler(0x80, syscall_handler);
}