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
#define STACK_SIZE 16384

class UserAddressSpace;
class ActiveFile;

class Process
{
public:
    Process(const char* name, void* entry, void* parameter = nullptr);
    ~Process();
    void Notify(int signal);

    // Kernel threads share KernelDirectory; userSpace owns isolated mappings.
    PageTableManager* pageTable;
    UserAddressSpace* userSpace = nullptr;
    uint64_t userPreemptions = 0;
    bool systemShell = false;
    ActiveFile* files[8] = {};
    uint64_t fileHandles[8] = {};

    uint64_t pid, rsp, rip, state;
    uint64_t* stack;
    void* parameter;
    char working_dir[256];
    char* name;
    Process* next;
};
extern uint64_t user_files_live;
uint64_t AddProcess(Process* process);
bool IsRunning(uint64_t pid);
bool IsTaskingOnline();
Process* GetRunningProcess();
void SendSignal(int signal);
void PrintAll();
[[noreturn]] void _Kill();
void Kill(uint64_t pid);
void ScheduleIRQ();
Registers* Schedule(Registers* frame);
void ReapExitedProcesses();
void InitialiseTasking();
#endif
