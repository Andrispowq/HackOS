#include "syscall.h"

#include "arch/x86_64/interrupts/idt.h"
#include "proc/tasking/process.h"
#include "lib/stdio.h"

static void* syscalls[HACKOS_SYSCALL_COUNT];
uint64_t num_syscalls = HACKOS_SYSCALL_COUNT;

static void syscall_handler(Registers* registers)
{
    if(registers->rax >= num_syscalls) //For now lets call this kprintf
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
    SetupSysret();
}