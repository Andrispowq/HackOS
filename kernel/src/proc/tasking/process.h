#ifndef PROCESS_H
#define PROCESS_H
#include "arch/x86_64/paging/paging.h"
#include "arch/x86_64/interrupts/idt.h"
#include "hackos/abi.h"

#define PROCESS_STATE_ALIVE 0
#define PROCESS_STATE_ZOMBIE 1
#define PROCESS_STATE_DEAD 2
#define PROCESS_STATE_WAITING 3
#define PROCESS_STATE_INPUT 4
#define PROCESS_STATE_STOPPED 5
#define PROCESS_STATE_IO 6
#define PROCESS_STATE_STARTING 7
#define PROCESS_STATE_SLEEP 8
#define SIG_ILL 1
#define SIG_TERM 2
#define SIG_SEGV 3
#define STACK_SIZE 16384

class UserAddressSpace;
class ActiveFile;
struct Stream;

struct ChildResult
{
    uint64_t pid = 0;
    int64_t status = 0;
    bool done = false;
    bool job = false;
    uint64_t group = 0, statusPid = 0;
};

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
    bool authenticationAgent = false;
    uint64_t sessionId = 0;
    HackosCredentials credentials = {};
    uint64_t terminalReturnPid = 0;
    uint64_t processGroup = 0, jobOwnerPid = 0, waitingGroup = 0;
    bool stoppedTerminalOwner = false;
    bool pipelineMember = false;
    bool jobWaitInterrupted = false;
    uint64_t parentPid = 0, waitingFor = 0;
    int64_t exitStatus = 0;
    ChildResult children[16] = {};

    bool waitTimed = false;
    uint64_t wakeTick = 0;
    Stream* descriptors[HACKOS_FD_LIMIT] = {};

    uint64_t windows[4] = {};

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
uint64_t WaitForChild(uint64_t pid, int64_t* status, uint64_t flags);
void SetTerminalOwner(uint64_t pid);
bool OwnsTerminal(uint64_t pid);
bool CanReadTerminal(uint64_t pid);
void RequestTerminalInterrupt();
void RequestUserInterrupt(uint64_t pid);
void RequestTerminalStop();
void WakeTerminalReaders();
uint64_t ReadTerminal(uint64_t flags);
uint64_t WaitForJob(uint64_t group, int64_t* status, uint64_t flags);
uint64_t ControlJob(uint64_t group, uint64_t operation);
uint64_t WaitForChildTimeout(uint64_t pid, int64_t* status, uint64_t flags, uint64_t timeout);
uint64_t WaitForJobTimeout(uint64_t group, int64_t* status, uint64_t flags, uint64_t timeout);
void WakeStreamWaiters();
uint64_t UserSleep(uint64_t timeout);
#endif
