#include "process.h"

#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"

#include "kernel.h"
#include "arch/x86_64/interrupts/idt.h"
#include "arch/x86_64/timer/pit.h"

Process* current_process;
Process* ready_queue;
uint64_t next_pid = 0;
uint8_t __enabled = 0;

extern uint64_t initial_rsp;
extern PageTableManager* CurrentDirectory;
extern PageTableManager KernelDirectory;

extern void EnableTasking();
extern void StartTask(uint64_t index);

void task_confirm()
{
	kprintf("Tasking is running!\n");
    _Kill();
}

void idle_thread()
{
    EnableTasking();
	__enabled = 1;
	while(true); //asm("hlt");
}

Process::Process(const char* name, void* rip, void* func_parameter)
    : name((char*)name)
{
    pid = ++next_pid;
    state = PROCESS_STATE_ALIVE;
	this->rip = (uint64_t)rip;
    
    next = nullptr;
    stack = (uint64_t*)kmalloc(STACK_SIZE);
    uint64_t* _stack = (uint64_t*)((void*)stack + 4096);
    uint64_t rbp = (uint64_t)_stack;
	*--_stack = 0x10; //ss
	*--_stack = (uint64_t)(_stack); //stack
	*--_stack = 0x0000000000000202; //rflags
	*--_stack = 0x8; //cs
	*--_stack = (uint64_t)rip; //rip
	*--_stack = 0; //err_code
	*--_stack = 0; //int_no
	*--_stack = 0; //r15
	*--_stack = 0; //r14
	*--_stack = 0; //r13
	*--_stack = 0; //r12
	*--_stack = 0; //r11
	*--_stack = 0; //r10
	*--_stack = 0; //r9
	*--_stack = 0; //r8
	*--_stack = 0; //rax
	*--_stack = 0; //rcx
	*--_stack = 0; //rdx
	*--_stack = 0; //rbx
	*--_stack = rbp; //rbp
	*--_stack = 0; //rsp
	*--_stack = 0; //rsi
	*--_stack = (uint64_t)func_parameter; //rdi -> we can have 1 parameter, which is put here
	_stack -= 32; //16 * 16 bytes -> 32 * 8 bytes
	memset(_stack, 0, 256);
	rsp = (uint64_t)_stack;

	memset(working_dir, 0, 256);
	strcpy(working_dir, "~/");

	pageTable = CurrentDirectory->Clone();
}

Process::~Process()
{
    delete pageTable;
    kfree((void*)stack);
}

void Process::Notify(int signal)
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
uint64_t AddProcess(Process* p)
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
	current_process->Notify(signal);
}

void PrintAll()
{
	Process* p = ready_queue;

	while(p)
	{
		kprintf("Process: %s (%d) %s\n", p->name, p->pid,
			p->state == PROCESS_STATE_ZOMBIE ? "ZOMBIE":
					p->state == PROCESS_STATE_ALIVE ? "ALIVE" : "DEAD");
		p = p->next;
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

	//kprintf("Killing process %s (%d)\n", current_process->name, current_process->pid);

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
    CurrentDirectory = current_process->pageTable;
	uint64_t pml4 = (uint64_t)CurrentDirectory->GetPML4();

	uint64_t new_stack = current_process->rsp;
	current_process->rsp += sizeof(Registers);

    StartProcess_FirstTime(new_stack, CurrentDirectory->PhysicalAddress(pml4));
}

void ScheduleIRQ()
{
	if(!__enabled) 
        return;
    
	asm volatile("int $0x20");
	return;
}

void Schedule()
{
	if(ready_queue->next == nullptr)
	{
		return; //We only have one task remaining, we can't schedule
	}

	uint64_t* old_stack = &current_process->rsp;

    current_process = current_process->next;
    if(!current_process) current_process = ready_queue;

    CurrentDirectory = current_process->pageTable;
	uint64_t pml4 = (uint64_t)CurrentDirectory->GetPML4();

	uint64_t new_stack = current_process->rsp;
	current_process->rsp += sizeof(Registers);

    StartProcess(new_stack, old_stack, CurrentDirectory->PhysicalAddress(pml4));
}

void InitialiseTasking()
{
    // Relocate the stack so we know where it is
	// This will become the kernel stack
    MoveStack((void*)0x000700000000000, 0x2000);

	current_process = new Process("kernel_idle", (void*)idle_thread);
    ready_queue = current_process;

	__AddProcess(new Process("task_confirm", (void*)task_confirm));
	__AddProcess(new Process("kernel", (void*)kernel_task));

    uint64_t old_stack_pointer; 
    asm volatile("mov %%rsp, %0" : "=r" (old_stack_pointer));
	__exec();

	kprintf("Failed to start tasking!");
}

void MoveStack(void* new_stack_start, uint64_t size)
{
	if(initial_rsp == 0)
	{			
		return;
	}

    for(uint64_t i = (uint64_t)new_stack_start; i >= ((uint64_t)new_stack_start - size);
        i -= 0x1000)
    {
        uint64_t addr = (uint64_t)PageFrameAllocator::SharedAllocator()->RequestPage();
        CurrentDirectory->MapMemory(i, addr, 0x3);
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
    for(uint64_t i = (uint64_t)new_stack_start; i > (uint64_t)new_stack_start - size; i -= 8)
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