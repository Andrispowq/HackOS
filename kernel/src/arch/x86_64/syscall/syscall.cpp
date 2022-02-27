#include "syscall.h"
#include "arch/x86_64/interrupts/idt.h"

#include "lib/stdio.h"

static void* syscalls[2] =
{
    (void*)&kprintf,
    (void*)&kprintf_backspace
};

uint32_t num_syscalls = 2;

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