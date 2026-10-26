#include "syscall.h"

#include "arch/x86_64/interrupts/idt.h"
#include "proc/tasking/process.h"
#include "lib/stdio.h"
#include "proc/user/user.h"

static void* syscalls[HACKOS_SYSCALL_COUNT];
uint64_t num_syscalls = HACKOS_SYSCALL_COUNT;

static void syscall_handler(Registers* registers)
{
    if ((registers->cs & 3) == 3)
    {
        if (registers->rax == HACKOS_SYS_exit) ExitUserProcess(registers->rdi);
        registers->rax = registers->rax == HACKOS_SYS_user_write
            ? user_write((const char*)registers->rdi, registers->rsi) : ~0ULL;
        return;
    }
    if(registers->rax >= num_syscalls)
    {
        registers->rax = (uint64_t)-1;
        return;
    }

    void* function = syscalls[registers->rax];

    uint64_t ret = ExecuteSyscall(registers->rdi, registers->rsi, registers->rdx, registers->rcx, registers->r8, (uint64_t)function);
    registers->rax = ret;
}

void InitialiseSyscalls()
{
#define HACKOS_SYSCALL(number, name, target, arity, ...) syscalls[number] = (void*)&target;
#include "hackos/syscalls.def"
#undef HACKOS_SYSCALL
    RegisterInterruptHandler(0x80, syscall_handler);
}