#include "process.h"
#include "hackos/abi.h"
#include "drivers/keyboard/keyboard.h"
#include "arch/x86_64/interrupts/guard.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "fs/filesystem.h"
#include "proc/user/address_space.h"
#include "proc/user/windows.h"
#include "arch/x86_64/gdt/gdt.h"

Process* current_process;
Process* ready_queue;
static uint64_t next_pid;
static bool enabled;
static uint64_t preemption_depth;
uint64_t scheduler_switches;
uint64_t tasks_reaped;
uint64_t user_files_live;
extern PageTableManager KernelDirectory;

static uint64_t terminalOwner, terminalShell, pendingInterrupt;

void SetTerminalOwner(uint64_t pid)
{
    InterruptGuard guard;
    if (!terminalShell)
    {
        terminalShell = pid; // first owner is the boot shell
    }

    if (pendingInterrupt == terminalOwner)
    {
        pendingInterrupt = 0;
    }

    terminalOwner = pid;
    ClearKeyboard(); // typeahead must never cross an ownership boundary
}

bool OwnsTerminal(uint64_t pid)
{
    return pid && pid == terminalOwner;
}

void RequestTerminalInterrupt()
{
    // Input dispatch latches a target only. Do not kill/yield while a
    // PreemptionGuard is active. Ctrl-C at the shell stays an event.
    if (terminalOwner && terminalOwner != terminalShell)
    {
        pendingInterrupt = terminalOwner;
        ClearKeyboard();
    }
}

void RequestUserInterrupt(uint64_t pid)
{
    if (pid && pid != terminalShell)
    {
        pendingInterrupt = pid;
    }
}

void DisablePreemption()
{
    InterruptGuard guard;
    ++preemption_depth;
}

void EnablePreemption()
{
    InterruptGuard guard;
    --preemption_depth;
}

static void TaskTrampoline(Process* process)
{
    ((void (*)(void*))process->rip)(process->parameter);
    _Kill();
}

Process::Process(const char* label, void* entry, void* parameter)
    : pageTable(&KernelDirectory), rsp(0), rip((uint64_t)entry), state(PROCESS_STATE_ALIVE), stack(nullptr),
      parameter(parameter), next(nullptr)
{
    PreemptionGuard guard;
    pid = ++next_pid;
    name = new char[strlen((char*)label) + 1];
    strcpy(name, label);
    memset(working_dir, 0, sizeof(working_dir));
    strcpy(working_dir, "~");
    // The bootstrap/idle thread keeps the boot stack. Its first IRQ saves rsp.
    if (!entry)
    {
        return;
    }

    stack = (uint64_t*)kmalloc(STACK_SIZE);
    uint64_t top = ((uint64_t)stack + STACK_SIZE) & ~15ULL;
    auto frame = (Registers*)(top - sizeof(Registers));
    memset(frame, 0, sizeof(*frame));
    *(uint16_t*)&frame->fx[0] = 0x037f;  // x87 control word
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
    ReleaseUserWindows(this);
    for (auto file : files)
    {
        if (file)
        {
            filesystems[0]->CloseFile(file);
            --user_files_live;
        }
    }

    delete userSpace;
    kfree(stack);
    delete[] name;
}

void Process::Notify(int signal)
{
    if (signal == SIG_ILL || signal == SIG_TERM || signal == SIG_SEGV)
    {
        Kill(pid);
    }
}

uint64_t AddProcess(Process* process)
{
    InterruptGuard guard;
    Process** tail = &ready_queue;
    while (*tail)
    {
        tail = &(*tail)->next;
    }

    *tail = process;
    return process->pid;
}

bool IsRunning(uint64_t pid)
{
    InterruptGuard guard;
    for (Process* p = ready_queue; p; p = p->next)
    {
        if (p->pid == pid)
        {
            return p->state == PROCESS_STATE_ALIVE || p->state == PROCESS_STATE_WAITING;
        }
    }

    return false;
}

bool IsTaskingOnline()
{
    return enabled;
}

Process* GetRunningProcess()
{
    return current_process;
}

void SendSignal(int signal)
{
    if (current_process)
    {
        current_process->Notify(signal);
    }
}

void PrintAll()
{
    PreemptionGuard guard;
    for (Process* p = ready_queue; p; p = p->next)
    {
        kprintf("Process: %s (%d) %s\n", p->name, p->pid, p->state == PROCESS_STATE_ALIVE ? "ALIVE" : "ZOMBIE");
    }
}

// Publish completion before idle destroys the child's stack/address space.
// All callers hold interrupts disabled on this single scheduling CPU.
static void CompleteProcess(Process* child)
{
    if (child->state == PROCESS_STATE_ZOMBIE)
    {
        return;
    }

    child->state = PROCESS_STATE_ZOMBIE;
    // A foreground launcher can die before its delegated terminal owner.
    // Redirect the return link before idle reclaims that launcher's record.
    uint64_t returnPid = child->terminalReturnPid ? child->terminalReturnPid : terminalShell;
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->terminalReturnPid == child->pid)
        {
            p->terminalReturnPid = returnPid;
        }
    }

    if (child->pid == terminalOwner)
    {
        SetTerminalOwner(returnPid);
    }

    for (auto parent = ready_queue; parent; parent = parent->next)
    {
        if (parent->pid == child->parentPid && parent->state != PROCESS_STATE_ZOMBIE)
        {
            for (auto& result : parent->children)
            {
                if (result.pid == child->pid)
                {
                    result.status = child->exitStatus;
                    result.done = true;
                    if (parent->state == PROCESS_STATE_WAITING && parent->waitingFor == child->pid)
                    {
                        parent->state = PROCESS_STATE_ALIVE;
                    }

                    return;
                }
            }
        }
    }
}

uint64_t WaitForChild(uint64_t pid, int64_t* status, uint64_t flags)
{
    InterruptGuard guard;
    auto parent = current_process;
    if (!parent || !parent->userSpace || flags & ~HACKOS_WAIT_NOHANG ||
        (status && !parent->userSpace->WritableRange((uint64_t)status, sizeof(*status))))
    {
        return HACKOS_ERROR;
    }

    ChildResult* result = nullptr;
    for (auto& child : parent->children)
    {
        if (child.pid && child.pid == pid)
        {
            result = &child;
            break;
        }
    }

    if (!result)
    {
        return HACKOS_ERROR; // never wait for self, a stranger, or an already consumed child
    }

    if (!result->done && (flags & HACKOS_WAIT_NOHANG))
    {
        return 0;
    }
    while (!result->done)
    {
        parent->waitingFor = pid;
        parent->state = PROCESS_STATE_WAITING;
        ScheduleIRQ(); // No PreemptionGuard: this task is genuinely blocked.
    }

    parent->waitingFor = 0;
    if (status && !parent->userSpace->CopyToUser((uint64_t)status, &result->status, sizeof(*status)))
    {
        return HACKOS_ERROR;
    }

    *result = {}; // status is consumed exactly once; idle independently reclaims resources
    return pid;
}

[[noreturn]] void _Kill()
{
    asm volatile("cli" ::: "memory");
    if (current_process && current_process != ready_queue)
    {
        CompleteProcess(current_process);
        ScheduleIRQ();
    }

    // The idle task must remain available; an exited task must never resume.
    for (;;)
    {
        asm volatile("hlt");
    }
}

void Kill(uint64_t pid)
{
    InterruptGuard guard;
    if (!current_process || pid == ready_queue->pid)
    {
        return;
    }

    if (pid == current_process->pid)
    {
        current_process->exitStatus = 137;
        _Kill();
    }

    for (Process* p = ready_queue; p; p = p->next)
    {
        if (p->pid == pid)
        {
            p->exitStatus = 137;
            CompleteProcess(p);
        }
    }
}

void ScheduleIRQ()
{
    asm volatile("int $0x81" ::: "memory");
}

Registers* Schedule(Registers* frame)
{
    // Called with interrupts disabled, after the PIC has been acknowledged.
    if (!enabled || preemption_depth)
    {
        return frame;
    }

    if (pendingInterrupt)
    {
        uint64_t target = pendingInterrupt;
        pendingInterrupt = 0;
        if (target != terminalShell)
        {
            for (auto p = ready_queue; p; p = p->next)
            {
                if (p->pid == target && p->state != PROCESS_STATE_ZOMBIE)
                {
                    p->exitStatus = 130;
                    CompleteProcess(p);
                    break;
                }
            }
        }
    }

    current_process->rsp = (uint64_t)frame;
    Process* next = current_process;
    do
    {
        next = next->next ? next->next : ready_queue;
    } while (next->state != PROCESS_STATE_ALIVE);
    if (next != current_process)
    {
        ++scheduler_switches;
        if (frame->int_no == IRQ0 && (frame->cs & 3) == 3)
        {
            ++current_process->userPreemptions;
        }
    }

    if (next->pageTable != current_process->pageTable)
    {
        next->pageTable->SetAsCurrent();
    }

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
        else
        {
            link = &p->next;
        }
    }
}

void InitialiseTasking()
{
    InterruptGuard guard;
    current_process = new Process("idle", nullptr);
    AddProcess(current_process);
    enabled = true;
}
