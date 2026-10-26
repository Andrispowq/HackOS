#include "process.h"
#include "arch/x86_64/interrupts/guard.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "kernel.h"
#include "proc/user/address_space.h"
#include "arch/x86_64/gdt/gdt.h"

Process* current_process;
Process* ready_queue;
static uint64_t next_pid;
static bool enabled;
static uint64_t preemption_depth;
uint64_t scheduler_switches;
uint64_t tasks_reaped;
extern PageTableManager KernelDirectory;

void DisablePreemption() { InterruptGuard guard; ++preemption_depth; }
void EnablePreemption() { InterruptGuard guard; --preemption_depth; }

static void TaskTrampoline(Process* process)
{
    ((void (*)(void*))process->rip)(process->parameter);
    _Kill();
}

Process::Process(const char* label, void* entry, void* parameter)
    : pageTable(&KernelDirectory), rsp(0), rip((uint64_t)entry),
      state(PROCESS_STATE_ALIVE), stack(nullptr), parameter(parameter), next(nullptr)
{
    PreemptionGuard guard;
    pid = ++next_pid;
    name = new char[strlen((char*)label) + 1];
    strcpy(name, label);
    memset(working_dir, 0, sizeof(working_dir));
    strcpy(working_dir, "~/");
    // The bootstrap/idle thread keeps the boot stack. Its first IRQ saves rsp.
    if (!entry) return;
    stack = (uint64_t*)kmalloc(STACK_SIZE);
    uint64_t top = ((uint64_t)stack + STACK_SIZE) & ~15ULL;
    auto frame = (Registers*)(top - sizeof(Registers));
    memset(frame, 0, sizeof(*frame));
    *(uint16_t*)&frame->fx[0] = 0x037f; // x87 control word
    *(uint32_t*)&frame->fx[24] = 0x1f80; // MXCSR
    frame->ds = frame->ss = 0x10;
    frame->cs = 0x08;
    frame->rflags = 0x202;
    frame->rip = (uint64_t)TaskTrampoline;
    frame->rdi = (uint64_t)this;
    frame->rsp = top - 8; // SysV function-entry alignment
    rsp = (uint64_t)frame;
}

Process::~Process()
{
    delete userSpace;
    kfree(stack);
    delete[] name;
}

void Process::Notify(int signal)
{
    if (signal == SIG_ILL || signal == SIG_TERM || signal == SIG_SEGV) Kill(pid);
}

uint64_t AddProcess(Process* process)
{
    InterruptGuard guard;
    Process** tail = &ready_queue;
    while (*tail) tail = &(*tail)->next;
    *tail = process;
    return process->pid;
}

bool IsRunning(uint64_t pid)
{
    InterruptGuard guard;
    for (Process* p = ready_queue; p; p = p->next)
        if (p->pid == pid) return p->state == PROCESS_STATE_ALIVE;
    return false;
}
bool IsTaskingOnline() { return enabled; }
Process* GetRunningProcess() { return current_process; }
void SendSignal(int signal) { if (current_process) current_process->Notify(signal); }

void PrintAll()
{
    PreemptionGuard guard;
    for (Process* p = ready_queue; p; p = p->next)
        kprintf("Process: %s (%d) %s\n", p->name, p->pid,
                p->state == PROCESS_STATE_ALIVE ? "ALIVE" : "ZOMBIE");
}

[[noreturn]] void _Kill()
{
    asm volatile("cli" ::: "memory");
    if (current_process && current_process != ready_queue)
    {
        current_process->state = PROCESS_STATE_ZOMBIE;
        ScheduleIRQ();
    }
    // The idle task must remain available; an exited task must never resume.
    for (;;) asm volatile("hlt");
}

void Kill(uint64_t pid)
{
    InterruptGuard guard;
    if (!current_process || pid == ready_queue->pid) return;
    if (pid == current_process->pid) _Kill();
    for (Process* p = ready_queue; p; p = p->next)
        if (p->pid == pid) p->state = PROCESS_STATE_ZOMBIE;
}

void ScheduleIRQ() { asm volatile("int $0x81" ::: "memory"); }

Registers* Schedule(Registers* frame)
{
    // Called with interrupts disabled, after the PIC has been acknowledged.
    if (!enabled || preemption_depth) return frame;
    current_process->rsp = (uint64_t)frame;
    Process* next = current_process;
    do { next = next->next ? next->next : ready_queue; }
    while (next->state != PROCESS_STATE_ALIVE);
    if (next != current_process)
    {
        ++scheduler_switches;
        if (frame->int_no == IRQ0 && (frame->cs & 3) == 3)
            ++current_process->userPreemptions;
    }
    if (next->pageTable != current_process->pageTable) next->pageTable->SetAsCurrent();
    SetKernelEntryStack(next->stack ? ((uint64_t)next->stack + STACK_SIZE) & ~15ULL : 0);
    current_process = next;
    return (Registers*)next->rsp;
}

void ReapExitedProcesses()
{
    // Only idle calls this, after the dying thread's interrupt frame was left.
    PreemptionGuard guard;
    Process** link = &ready_queue;
    while (*link)
    {
        Process* p = *link;
        if (p != current_process && p->state == PROCESS_STATE_ZOMBIE)
        {
            *link = p->next;
            delete p;
            ++tasks_reaped;
        }
        else link = &p->next;
    }
}

void InitialiseTasking()
{
    InterruptGuard guard;
    current_process = new Process("idle", nullptr);
    AddProcess(current_process);
    AddProcess(new Process("kernel shell", (void*)kernel_task));
    enabled = true;
}
