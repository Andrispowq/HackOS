#include "process.h"

#include "lib/stdio.h"
#include "lib/memory.h"

Process* current_process;
Process* ready_queue;
uint64_t next_pid = 0;
uint8_t __enabled = 0;

extern uint64_t initial_rsp;
extern PageTableManager* CurrentDirectory;
extern PageTableManager KernelDirectory;

extern void EnableTasking();
extern void StartTask(uint64_t index);

Process::Process(const char* name, void* rip)
    : name((char*)name), rip((uint64_t)rip)
{
    pid = ++next_pid;
    state = PROCESS_STATE_ALIVE;
    
    next = nullptr;
    rsp = kmalloc(STACK_SIZE);
    uint64_t* stack = (uint64_t*)(rsp + 4096);
    rbp = (uint64_t)stack;
    original_stack_top = rbp;
	*--stack = 0x00000202; //rflags
	*--stack = 0x8; //cs
	*--stack = (uint64_t)rip; //rip
	*--stack = 0; //rax
	*--stack = 0; //rbx
	*--stack = 0; //rcx;
	*--stack = 0; //rdx
	*--stack = 0; //rsi
	*--stack = 0; //rdi
	*--stack = rsp + 4096; //rbp
	*--stack = 0x10; //ds
	*--stack = 0x10; //fs
	*--stack = 0x10; //es
	*--stack = 0x10; //gs
	rsp = (uint64_t)stack;

	pageTable = CurrentDirectory->Clone();
}

Process::~Process()
{
    delete pageTable;
    kfree((void*)original_stack_top);
}

void Process::notify(int signal)
{
	switch(signal)
	{
	case SIG_ILL:
		kprintf("Received SIGILL, terminating!\n");
		_Kill();
		break;
	case SIG_TERM:
		kprintf("Received SIGTERM, terminating!\n");
		_Kill();
	case SIG_SEGV:
		kprintf("Received SIGSEGV, terminating!\n");
		_Kill();
	default:
		kprintf("Received unknown SIG!\n");
		return;
	}
}

/* This adds a process while no others are running! */
void __AddProcess(Process* p)
{
    Process* tmp = ready_queue;
    while (tmp->next)
       tmp = tmp->next;

    tmp->next = p;
    return;
}

/* add process but take care of others also! */
uint64_t addProcess(Process* p)
{
	StartTask(0);
	__AddProcess(p);
	StartTask(1);
	return p->pid;
}

bool IsRunning(uint64_t pid)
{
	StartTask(0);

	Process* p = current_process;
	Process* orig = current_process;

	bool ret = false;
	while(true)
	{
		if(p->pid == pid)  
        { 
            ret = true; 
            break;
        }

		p = p->next;
		if(p == orig) break;
	}

	StartTask(1);
	return ret;
}

bool IsTaskingOnline()
{
	return __enabled;
}

Process* GetRunningProcess()
{
	return current_process;
}

void SendSignal(int signal)
{
	current_process->notify(signal);
}

void PrintAll()
{
	Process* orig = current_process;
	Process* p = orig;

	while(true)
	{
		kprintf("Process: %s (%d) %s\n", p->name, p->pid,
			p->state == PROCESS_STATE_ZOMBIE ? "ZOMBIE":
					p->state==PROCESS_STATE_ALIVE ? "ALIVE" : "DEAD");
		p = p->next;

		if(p == orig) 
            break;
	}
}

void _Kill()
{
	asm volatile("cli");

	if(current_process->pid == 1) 
    { 
        StartTask(0); 
        kprintf("Idle can't be killed!\n");
        while(1) asm volatile("hlt");
    }

	kprintf("Killing process %s (%d)\n", current_process->name, current_process->pid);

	StartTask(0);
	kfree(current_process->pageTable);

    Process* tmp = ready_queue;
    while(tmp)
    {
        if(tmp->next == current_process)
        {
            tmp->next = current_process->next;
        }

        tmp = tmp->next;
    }

	delete current_process;
	StartTask(1);

	asm volatile("sti");

	ScheduleIRQ();
}

void Kill(uint64_t pid)
{
	if(pid == 1) kprintf("Idle can't be killed!\n");
	if(pid == current_process->pid) _Kill();

	Process* orig = current_process;
	Process* curr = orig;

	while(curr)
	{
		if(curr->pid == pid) 
        {
			kprintf("Process %s (%d) was set to ZOMBIE.\n", curr->name, pid);
			curr->state = PROCESS_STATE_ZOMBIE;
			break;
		}

		curr = curr->next;
	}
}

void __exec()
{
    uint32_t rsp, rbp, rip;
    rip = current_process->rip;
    rsp = current_process->rsp;
	rbp = current_process->rbp;

    CurrentDirectory = current_process->pageTable;
	uint64_t pml4 = (uint64_t)CurrentDirectory->GetPML4();
    JumpToAddress(rip, CurrentDirectory->PhysicalAddress(pml4), rbp, rsp);
}

void ScheduleIRQ()
{
	if(!__enabled) 
        return;
    
	asm volatile("int $0x2E");
	return;
}

void Schedule()
{
    current_process = current_process->next;
    if(!current_process) current_process = ready_queue; //If we're at the end, start over again

    uint32_t rsp, rbp, rip;
    rip = current_process->rip;
    rsp = current_process->rsp;
    rbp = current_process->rbp;

    CurrentDirectory = current_process->pageTable;
	uint64_t pml4 = (uint64_t)CurrentDirectory->GetPML4();
    JumpToAddress(rip, CurrentDirectory->PhysicalAddress(pml4), rbp, rsp);
}

void task_confirm()
{
	kprintf("Tasking is running!\n");
    _Kill();
}

void idle_thread()
{
    EnableTasking();
	__enabled = 1;
    while(true);
}

void InitialiseTasking()
{
    // Relocate the stack so we know where it is
    //MoveStack((void*)0x000700000000000, 0x2000);

	current_process = new Process("kernel_idle", (void*)idle_thread);
    ready_queue = current_process;

	__AddProcess(new Process("task_confirm", (void*)task_confirm));
	//__AddProcess(new Process("kernel", kernel_task));
	__exec();

    asm volatile("sti");

	kprintf("Failed to start tasking!");
}

void MoveStack(void* new_stack_start, uint64_t size)
{
    uint64_t i;

    for(i = (uint64_t)new_stack_start; i >= ((uint64_t)new_stack_start - size);
        i -= 0x1000)
    {
        uint64_t addr = (uint64_t)PageFrameAllocator::SharedAllocator()->RequestPage();
        CurrentDirectory->MapMemory(i, addr);
    }

    // Flush the TLB by reading and writing the page directory address again.
    uint64_t pd_addr;
    asm volatile("mov %%cr3, %0" : "=r" (pd_addr));
    asm volatile("mov %0, %%cr3" : : "r" (pd_addr)); 

    uint64_t old_stack_pointer; 
    asm volatile("mov %%rsp, %0" : "=r" (old_stack_pointer));

    uint64_t old_base_pointer;  
    asm volatile("mov %%rbp, %0" : "=r" (old_base_pointer));

    uint64_t offset = (uint64_t)new_stack_start - initial_rsp; 
    uint64_t new_stack_pointer = old_stack_pointer + offset;
    uint64_t new_base_pointer = old_base_pointer + offset; 

    memcpy((void*)new_stack_pointer, (void*)old_stack_pointer, initial_rsp - old_stack_pointer);

    // Backtrace through the original stack, copying new values into
    // the new stack.
    for(i = (uint64_t)new_stack_start; i > (uint64_t)new_stack_start - size; i -= 4)
    {
        uint64_t tmp = *(uint64_t*)i;

        if ((old_stack_pointer < tmp) && (tmp < initial_rsp))
        {
            tmp = tmp + offset;
            uint64_t* tmp2 = (uint64_t*)i;
            *tmp2 = tmp;
        }
    }

    // Change stacks.
    asm volatile("mov %0, %%rsp" : : "r" (new_stack_pointer));
    asm volatile("mov %0, %%rbp" : : "r" (new_base_pointer));
}