#ifndef PROCESS_H
#define PROCESS_H

#include "arch/x86_64/paging/paging.h"
#include "arch/x86_64/interrupts/idt.h"

#define PROCESS_STATE_ALIVE 0
#define PROCESS_STATE_ZOMBIE 1
#define PROCESS_STATE_DEAD 2

#define SIG_ILL 1
#define SIG_TERM 2
#define SIG_SEGV 3

#define STACK_SIZE 4096

extern "C" void StartProcess(uint64_t pml4, uint64_t stack_top);

class Process
{
public:
    Process(const char* name, void* rip);
    ~Process();

    void Notify(int signal);

public:
    PageTableManager* pageTable;
    uint64_t pid;
    uint64_t rsp, rbp, rip;
    uint64_t original_stack_top;
    uint64_t state;

    char* name;
    Process* next;
};

uint64_t AddProcess(Process* process);

bool IsRunning(uint64_t pid);
bool IsTaskingOnline();

Process* GetRunningProcess();

void SendSignal(int signal);

void PrintAll();

void _Kill();
void Kill();
void ScheduleIRQ();
void Schedule(Registers* regs);
void InitialiseTasking();

void MoveStack(void* new_stack_start, uint64_t size);

#endif