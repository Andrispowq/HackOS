#include "net/ping.h"
#include "net/dns.h"
#include "net/socket.h"
#include "process.h"
#include "proc/user/terminals.h"
#include "deadline.h"
#include "proc/user/streams.h"
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

static uint64_t terminalOwner, terminalShell, pendingInterrupt, pendingGroupInterrupt, pendingStop;

uint64_t ConsoleTerminalOwner()
{
    return terminalOwner;
}

static Process* FindProcess(uint64_t pid)
{
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->pid == pid && p->state != PROCESS_STATE_ZOMBIE)
        {
            return p;
        }
    }

    return nullptr;
}

static unsigned JobState(uint64_t group)
{
    unsigned state = 0;
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->processGroup == group && p->state != PROCESS_STATE_ZOMBIE)
        {
            if (p->state != PROCESS_STATE_STOPPED)
            {
                return 1;
            }

            state = 2;
        }
    }

    return state;
}

static void WakeJobWaiters()
{
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->state == PROCESS_STATE_WAITING && p->waitingGroup && JobState(p->waitingGroup) != 1)
        {
            p->state = PROCESS_STATE_ALIVE;
        }
    }
}

void WakeStreamWaiters()
{
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->state == PROCESS_STATE_IO)
        {
            p->state = PROCESS_STATE_ALIVE;
        }
    }
}

uint64_t UserSleep(uint64_t timeout)
{
    InterruptGuard guard;
    WaitDeadline deadline(timeout);
    if (!deadline.Valid() || timeout == HACKOS_WAIT_FOREVER)
    {
        return HACKOS_ERROR;
    }

    while (!deadline.Expired())
    {
        deadline.Block(PROCESS_STATE_SLEEP);
    }

    return 0;
}

void WakeTerminalReaders()
{
    WakeStreamWaiters();
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->state == PROCESS_STATE_INPUT)
        {
            p->state = PROCESS_STATE_ALIVE;
        }
    }
}

uint64_t ReadTerminal(uint64_t flags)
{
    InterruptGuard guard;
    if (flags & ~HACKOS_INPUT_NONBLOCK)
    {
        return HACKOS_ERROR;
    }

    for (;;)
    {
        if (!CanReadTerminal(current_process->pid))
        {
            return HACKOS_DENIED;
        }

        char ch;
        auto result = ReadProcessTerminal(ch);
        if (result == KeyboardRead::Overflow)
        {
            return HACKOS_INPUT_OVERFLOW;
        }

        if (result == KeyboardRead::Character)
        {
            return (uint8_t)ch;
        }

        if (flags & HACKOS_INPUT_NONBLOCK)
        {
            return 0;
        }

        // Interrupts stay masked from the empty check through the state change.
        // The nested scheduling interrupt resumes here after a producer wakes us.
        current_process->state = PROCESS_STATE_INPUT;
        ScheduleIRQ();
    }
}

void SetTerminalOwner(uint64_t pid)
{
    InterruptGuard guard;
    auto target = FindProcess(pid);
    auto tty = ProcessTerminal(target);
    uint64_t& ownerSlot = tty ? tty->owner : terminalOwner;
    if (!terminalShell)
    {
        terminalShell = pid; // first owner is the login agent
    }

    if (pendingInterrupt == ownerSlot)
    {
        pendingInterrupt = 0;
    }

    auto previous = FindProcess(ownerSlot);
    auto owner = FindProcess(pid);
    bool samePipeline = previous && owner && previous->pipelineMember && owner->pipelineMember &&
                        previous->processGroup && previous->processGroup == owner->processGroup;
    if (owner)
    {
        for (auto p = ready_queue; p; p = p->next)
        {
            if (p->processGroup == owner->processGroup)
            {
                p->stoppedTerminalOwner = p == owner;
            }
        }
    }

    WakeTerminalReaders();
    ownerSlot = pid;
    if (!samePipeline)
    {
        ClearTerminalInput(target); // preserve keys only across handoff between pipeline peers
    }
}

bool OwnsTerminal(uint64_t pid)
{
    auto process = FindProcess(pid);
    return pid && pid == TerminalOwner(process);
}

bool CanReadTerminal(uint64_t pid)
{
    if (OwnsTerminal(pid))
    {
        return true;
    }
    auto process = FindProcess(pid);
    auto owner = FindProcess(TerminalOwner(process));
    // Foreground pipeline peers share input, so a pager can read keys while an
    // upstream stage still produces data. This capability is not inherited by
    // ordinary children: explicit foreground delegation retains its contract.
    return process && owner && process->pipelineMember && owner->pipelineMember && process->processGroup &&
           process->processGroup == owner->processGroup;
}

static bool ProtectedTerminal(uint64_t pid)
{
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->pid == pid)
        {
            return p->systemShell && !p->desktopController;
        }
    }

    return false;
}

void RequestTerminalInterrupt(uint64_t target)
{
    // Input dispatch latches a target only. Do not kill/yield while a
    // PreemptionGuard is active. Ctrl-C can also cancel the shell's wait
    // without terminating the background group it was observing.
    auto owner = FindProcess(target ? target : terminalOwner);
    if (owner && owner->systemShell && owner->waitingGroup)
    {
        owner->jobWaitInterrupted = true;
        owner->state = PROCESS_STATE_ALIVE;
        ClearTerminalInput(owner);
    }
    else if (owner && !owner->systemShell)
    {
        pendingGroupInterrupt = owner->processGroup;
        ClearTerminalInput(owner);
    }
}

void RequestTerminalStop(uint64_t target)
{
    auto owner = FindProcess(target ? target : terminalOwner);
    if (owner && !owner->systemShell && owner->jobOwnerPid)
    {
        pendingStop = owner->processGroup;
        ClearTerminalInput(owner);
    }
}

void RequestUserInterrupt(uint64_t pid)
{
    if (pid && !ProtectedTerminal(pid))
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
    Net::CancelEcho(pid);
    Net::CancelResolve(pid);
    Net::CloseUdpSockets(pid);
    ReleaseUserWindows(this);
    ReleaseStreams(this);
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
            return p->state != PROCESS_STATE_ZOMBIE && p->state != PROCESS_STATE_DEAD;
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
    // Pipe peers must observe exit immediately, independently of idle reaping.
    ReleaseStreams(child, true);
    // A nested shell/controller owns the groups it explicitly created. End
    // those jobs before discarding its completion records and terminal link.
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p != child && p->jobOwnerPid == child->pid && p->state != PROCESS_STATE_ZOMBIE)
        {
            p->exitStatus = 137;
            CompleteProcess(p);
        }
    }

    if (child->sessionId == child->pid && !child->authenticationAgent)
    {
        ClearSessionClipboard(child->sessionId);
        // Include detached/background descendants and runas identities. The
        // session ID is kernel-owned and cannot be changed by a user program.
        for (auto p = ready_queue; p; p = p->next)
        {
            if (p != child && p->sessionId == child->sessionId && p->state != PROCESS_STATE_ZOMBIE)
            {
                p->exitStatus = 137;
                CompleteProcess(p);
            }
        }
    }

    ReleaseTerminalController(child);

    // A foreground launcher can die before its delegated terminal owner.
    // Redirect the return link before idle reclaims that launcher's record.
    uint64_t returnPid = child->terminalReturnPid
                             ? child->terminalReturnPid
                             : (ProcessTerminal(child) ? ProcessTerminal(child)->shell : terminalShell);
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->terminalReturnPid == child->pid)
        {
            p->terminalReturnPid = returnPid;
        }
    }

    if (child->pid == TerminalOwner(child))
    {
        auto receiver = FindProcess(returnPid);
        if (!receiver || receiver->processGroup != child->processGroup)
        {
            for (auto p = ready_queue; p; p = p->next)
            {
                if (p->processGroup == child->processGroup && p->state != PROCESS_STATE_ZOMBIE &&
                    p->state != PROCESS_STATE_STOPPED)
                {
                    returnPid = p->pid;
                    if (!p->terminalReturnPid)
                    {
                        p->terminalReturnPid = child->jobOwnerPid;
                    }

                    break;
                }
            }
        }

        auto tty = ProcessTerminal(child);
        auto terminalReceiver = FindProcess(returnPid);
        if (tty && (!terminalReceiver || terminalReceiver->terminalId != child->terminalId))
        {
            tty->owner = 0;
        }
        else
        {
            SetTerminalOwner(returnPid);
        }
    }

    WakeJobWaiters();

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
    return WaitForChildTimeout(pid, status, flags, HACKOS_WAIT_FOREVER);
}

uint64_t WaitForChildTimeout(uint64_t pid, int64_t* status, uint64_t flags, uint64_t timeout)
{
    InterruptGuard guard;
    auto parent = current_process;
    WaitDeadline deadline(timeout);
    if (!deadline.Valid() || !parent || !parent->userSpace || flags & ~HACKOS_WAIT_NOHANG ||
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
        if (deadline.Expired())
        {
            parent->waitingFor = 0;
            return HACKOS_TIMED_OUT;
        }

        parent->waitingFor = pid;
        deadline.Block(PROCESS_STATE_WAITING);
    }

    parent->waitingFor = 0;
    if (status && !parent->userSpace->CopyToUser((uint64_t)status, &result->status, sizeof(*status)))
    {
        return HACKOS_ERROR;
    }

    *result = {}; // status is consumed exactly once; idle independently reclaims resources
    return pid;
}

static ChildResult* OwnedJob(uint64_t group)
{
    if (current_process->authenticationAgent || !group)
    {
        return nullptr;
    }

    for (auto& result : current_process->children)
    {
        if (result.pid == group && result.job)
        {
            return &result;
        }
    }

    return nullptr;
}

uint64_t WaitForJob(uint64_t group, int64_t* status, uint64_t flags)
{
    return WaitForJobTimeout(group, status, flags, HACKOS_WAIT_FOREVER);
}

uint64_t WaitForJobTimeout(uint64_t group, int64_t* status, uint64_t flags, uint64_t timeout)
{
    InterruptGuard guard;
    auto result = OwnedJob(group);
    WaitDeadline deadline(timeout);
    if (!deadline.Valid() || !result || (flags & ~HACKOS_WAIT_NOHANG) ||
        (status && !current_process->userSpace->WritableRange((uint64_t)status, sizeof(*status))))
    {
        return HACKOS_ERROR;
    }

    current_process->jobWaitInterrupted = false;
    while (JobState(group) == 1 && !current_process->jobWaitInterrupted)
    {
        if (flags & HACKOS_WAIT_NOHANG)
        {
            return 0;
        }

        if (deadline.Expired())
        {
            current_process->waitingGroup = 0;
            return HACKOS_TIMED_OUT;
        }

        current_process->waitingGroup = group;
        deadline.Block(PROCESS_STATE_WAITING);
    }

    current_process->waitingGroup = 0;
    if (current_process->jobWaitInterrupted)
    {
        current_process->jobWaitInterrupted = false;
        return HACKOS_JOB_INTERRUPTED;
    }

    bool stopped = JobState(group) == 2;
    int64_t value = stopped ? 148 : result->status;
    if (!stopped && result->statusPid)
    {
        for (auto& member : current_process->children)
        {
            if (member.pid == result->statusPid)
            {
                value = member.status;
            }
        }
    }
    if (status)
    {
        current_process->userSpace->CopyToUser((uint64_t)status, &value, sizeof(value));
    }

    if (!stopped)
    {
        for (auto& member : current_process->children)
        {
            if (member.group == group)
            {
                member = {};
            }
        }

        *result = {};
    }

    return stopped ? HACKOS_JOB_STOPPED : group;
}

uint64_t ControlJob(uint64_t group, uint64_t operation)
{
    InterruptGuard guard;
    if (!OwnedJob(group))
    {
        return HACKOS_DENIED;
    }

    if (operation > HACKOS_JOB_QUERY)
    {
        return HACKOS_ERROR;
    }

    if (operation == HACKOS_JOB_QUERY)
    {
        return JobState(group);
    }

    if (!OwnsTerminal(current_process->pid))
    {
        return HACKOS_DENIED;
    }

    Process* reader = nullptr;
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->processGroup != group || p->state == PROCESS_STATE_ZOMBIE)
        {
            continue;
        }

        if (operation == HACKOS_JOB_TERMINATE)
        {
            p->exitStatus = 137;
            CompleteProcess(p);
            continue;
        }

        if (!reader || p->stoppedTerminalOwner)
        {
            reader = p;
        }

        if (p->state == PROCESS_STATE_STOPPED || p->state == PROCESS_STATE_STARTING)
        {
            // Recheck a suspended wait/read predicate, including a child that
            // completed while its parent was stopped.
            p->state = PROCESS_STATE_ALIVE;
        }
    }

    if (operation == HACKOS_JOB_TERMINATE)
    {
        return 0;
    }

    if (!reader)
    {
        return HACKOS_ERROR;
    }

    if (operation == HACKOS_JOB_FOREGROUND)
    {
        reader->terminalReturnPid = current_process->pid;
        SetTerminalOwner(reader->pid);
    }

    return 0;
}

static void StopJob(uint64_t group)
{
    uint64_t owner = 0;
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->processGroup == group && !p->systemShell && p->state != PROCESS_STATE_ZOMBIE)
        {
            owner = p->jobOwnerPid;
            p->stoppedTerminalOwner = p->pid == TerminalOwner(p);
            p->state = PROCESS_STATE_STOPPED;
        }
    }

    if (owner)
    {
        SetTerminalOwner(owner);
        WakeJobWaiters();
    }
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

void TerminateProcess(uint64_t pid)
{
    InterruptGuard guard;
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->pid == pid && p != ready_queue)
        {
            p->exitStatus = 137;
            CompleteProcess(p);
            return;
        }
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

    if (pendingGroupInterrupt)
    {
        uint64_t group = pendingGroupInterrupt;
        pendingGroupInterrupt = 0;
        pendingStop = 0;
        for (auto p = ready_queue; p; p = p->next)
        {
            if (p->processGroup == group && !p->systemShell && p->state != PROCESS_STATE_ZOMBIE)
            {
                p->exitStatus = 130;
                CompleteProcess(p);
            }
        }
    }

    if (pendingStop)
    {
        uint64_t group = pendingStop;
        pendingStop = 0;
        StopJob(group);
    }

    if (pendingInterrupt)
    {
        uint64_t target = pendingInterrupt;
        pendingInterrupt = 0;
        if (!ProtectedTerminal(target))
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

    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->waitTimed &&
            (p->state == PROCESS_STATE_WAITING || p->state == PROCESS_STATE_IO || p->state == PROCESS_STATE_SLEEP) &&
            (int64_t)(tick - p->wakeTick) >= 0)
        {
            p->state = PROCESS_STATE_ALIVE;
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
