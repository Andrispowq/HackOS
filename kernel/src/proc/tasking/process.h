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

extern "C" void StartProcess(uint64_t new_stack, uint64_t* old_stack, uint64_t pml4);
extern "C" void StartProcess_FirstTime(uint64_t new_stack, uint64_t pml4);

class Process
{
public:
    Process(const char* name, void* rip);
    ~Process();

    void Notify(int signal);

public:
    PageTableManager* pageTable;
    uint64_t pid;
    uint64_t rsp, rip;
    uint64_t state;

    uint64_t* stack;
    char working_dir[256];

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
void Schedule();
void InitialiseTasking();

void MoveStack(void* new_stack_start, uint64_t size);

#endif