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
	while(true) asm("hlt");
}

Process::Process(const char* name, void* rip)
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
	*--_stack = (uint64_t)_stack; //stack
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
	*--_stack = 0; //rdi
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
	Process* orig = current_process;
	Process* p = orig;

	while(true)
	{
		kprintf("Process: %s (%d) %s\n", p->name, p->pid,
			p->state == PROCESS_STATE_ZOMBIE ? "ZOMBIE":
					p->state == PROCESS_STATE_ALIVE ? "ALIVE" : "DEAD");
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
    /*CurrentDirectory = current_process->pageTable;
	uint64_t pml4 = (uint64_t)CurrentDirectory->GetPML4();

    StartProcess_FirstTime(current_process->rsp, CurrentDirectory->PhysicalAddress(pml4));*/
	uint64_t pml4 = (uint64_t)CurrentDirectory->GetPML4();

	asm volatile("mov %%rax, %%cr3": :"a"(pml4));
	asm volatile("mov %%rax, %%rsp": :"a"(current_process->rsp));

	asm volatile("movdqa 240(%rsp), %xmm0");
	asm volatile("movdqa 224(%rsp), %xmm1");
	asm volatile("movdqa 208(%rsp), %xmm2");
	asm volatile("movdqa 192(%rsp), %xmm3");
	asm volatile("movdqa 176(%rsp), %xmm4");
	asm volatile("movdqa 160(%rsp), %xmm5");
	asm volatile("movdqa 144(%rsp), %xmm6");
	asm volatile("movdqa 128(%rsp), %xmm7");

	asm volatile("movdqa 112(%rsp), %xmm8");
	asm volatile("movdqa 96(%rsp), %xmm9");
	asm volatile("movdqa 80(%rsp), %xmm10");
	asm volatile("movdqa 64(%rsp), %xmm11");
	asm volatile("movdqa 48(%rsp), %xmm12");
	asm volatile("movdqa 32(%rsp), %xmm13");
	asm volatile("movdqa 16(%rsp), %xmm14");
	asm volatile("movdqa 0(%rsp), %xmm15");

	asm volatile("add $256, %rsp");

	asm volatile("pop %rdi");
	asm volatile("pop %rsi");
	asm volatile("add $0x8, %rsp");
	asm volatile("pop %rbp");
	asm volatile("pop %rbx");
	asm volatile("pop %rdx");
	asm volatile("pop %rcx");
	asm volatile("pop %rax");

	asm volatile("pop %r8");
	asm volatile("pop %r9");
	asm volatile("pop %r10");
	asm volatile("pop %r11");
	asm volatile("pop %r12");
	asm volatile("pop %r13");
	asm volatile("pop %r14");
	asm volatile("pop %r15");

	asm volatile("add $0x10, %rsp");

    asm volatile("sti");
    asm volatile("iretq");
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
	asm volatile("mov %rbp, %rsp");
	asm volatile("add $0x10, %rsp");

	asm volatile("push %r15");
	asm volatile("push %r14");
	asm volatile("push %r13");
	asm volatile("push %r12");
	asm volatile("push %r11");
	asm volatile("push %r10");
	asm volatile("push %r9");
	asm volatile("push %r8");

	asm volatile("push %rax");
	asm volatile("push %rcx");
	asm volatile("push %rdx");
	asm volatile("push %rbx");
	asm volatile("push %rbp");
	asm volatile("push %rsp");
	asm volatile("push %rsi");
	asm volatile("push %rdi");

	asm volatile("sub $256, %rsp");

	asm volatile("movdqa %xmm0, 240(%rsp)");
	asm volatile("movdqa %xmm1, 224(%rsp)");
	asm volatile("movdqa %xmm2, 208(%rsp)");
	asm volatile("movdqa %xmm3, 192(%rsp)");
	asm volatile("movdqa %xmm4, 176(%rsp)");
	asm volatile("movdqa %xmm5, 160(%rsp)");
	asm volatile("movdqa %xmm6, 144(%rsp)");
	asm volatile("movdqa %xmm7, 128(%rsp)");

	asm volatile("movdqa %xmm8, 112(%rsp)");
	asm volatile("movdqa %xmm9, 96(%rsp)");
	asm volatile("movdqa %xmm10, 80(%rsp)");
	asm volatile("movdqa %xmm11, 64(%rsp)");
	asm volatile("movdqa %xmm12, 48(%rsp)");
	asm volatile("movdqa %xmm13, 32(%rsp)");
	asm volatile("movdqa %xmm14, 16(%rsp)");
	asm volatile("movdqa %xmm15, 0(%rsp)");

	asm volatile("mov %%rsp, %%rax" : "=a"(current_process->rsp));

    current_process = current_process->next;
    if(!current_process) current_process = ready_queue;

	kprintf("Starting %s (PID %x)\n", current_process->name, current_process->pid);

	uint64_t pml4 = (uint64_t)CurrentDirectory->GetPML4();

	asm volatile("mov %%rax, %%cr3": :"a"(pml4));
	asm volatile("mov %%rax, %%rsp": :"a"(current_process->rsp));

	asm volatile("movdqa 240(%rsp), %xmm0");
	asm volatile("movdqa 224(%rsp), %xmm1");
	asm volatile("movdqa 208(%rsp), %xmm2");
	asm volatile("movdqa 192(%rsp), %xmm3");
	asm volatile("movdqa 176(%rsp), %xmm4");
	asm volatile("movdqa 160(%rsp), %xmm5");
	asm volatile("movdqa 144(%rsp), %xmm6");
	asm volatile("movdqa 128(%rsp), %xmm7");

	asm volatile("movdqa 112(%rsp), %xmm8");
	asm volatile("movdqa 96(%rsp), %xmm9");
	asm volatile("movdqa 80(%rsp), %xmm10");
	asm volatile("movdqa 64(%rsp), %xmm11");
	asm volatile("movdqa 48(%rsp), %xmm12");
	asm volatile("movdqa 32(%rsp), %xmm13");
	asm volatile("movdqa 16(%rsp), %xmm14");
	asm volatile("movdqa 0(%rsp), %xmm15");

	asm volatile("add $256, %rsp");

	asm volatile("out %%al, %%dx": :"d"(0x20), "a"(0x20)); // send EoI to master PIC

	asm volatile("pop %rdi");
	asm volatile("pop %rsi");
	asm volatile("add $0x8, %rsp");
	asm volatile("pop %rbp");
	asm volatile("pop %rbx");
	asm volatile("pop %rdx");
	asm volatile("pop %rcx");
	asm volatile("pop %rax");

	asm volatile("pop %r8");
	asm volatile("pop %r9");
	asm volatile("pop %r10");
	asm volatile("pop %r11");
	asm volatile("pop %r12");
	asm volatile("pop %r13");
	asm volatile("pop %r14");
	asm volatile("pop %r15");

	asm volatile("add $0x10, %rsp");

    asm volatile("sti");
    asm volatile("iretq");

    /*CurrentDirectory = current_process->pageTable;
	uint64_t pml4 = (uint64_t)CurrentDirectory->GetPML4();

    StartProcess(current_process->rsp, CurrentDirectory->PhysicalAddress(pml4));*/
}

void InitialiseTasking()
{
    // Relocate the stack so we know where it is
    MoveStack((void*)0x000700000000000, 0x2000);

	current_process = new Process("kernel_idle", (void*)idle_thread);
    ready_queue = current_process;

	__AddProcess(new Process("task_confirm", (void*)task_confirm));
	__AddProcess(new Process("kernel", (void*)kernel_task));
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