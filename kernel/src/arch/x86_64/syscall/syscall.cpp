#include "syscall.h"
#include "hackos/syscall.h"
#include "hackos/runtime/path.h"
#include "arch/x86_64/interrupts/guard.h"
#include "arch/x86_64/timer/rtc.h"
#include "arch/x86_64/timer/pit.h"
#include "arch/x86_64/ports.h"
#include "proc/tasking/process.h"
#include "proc/user/user.h"
#include "proc/user/accounts.h"
#include "proc/user/windows.h"
#include "proc/user/address_space.h"
#include "drivers/keyboard/keyboard.h"
#include "drivers/screen/screen.h"
#include "fs/filesystem.h"
#include "init/kernelInit.h"
#include "acpi/fadt.h"
#include "lib/stdio.h"
#include "lib/memory.h"

extern Process* ready_queue;
extern KernelInfo* kInfo;
static uint64_t nextHandle;

static Process* Caller()
{
    return GetRunningProcess();
}

static bool IsRoot()
{
    return Caller()->credentials.uid == HACKOS_ROOT_UID;
}

static bool Path(const char* address, char (&path)[256])
{
    char input[256];
    return Caller()->userSpace->CopyString(input, (uint64_t)address, sizeof(input)) && filesystems[0] &&
           NormalizePath(input, Caller()->working_dir, path);
}

static bool Output(void* destination, const void* source, uint64_t length)
{
    return Caller()->userSpace->CopyToUser((uint64_t)destination, source, length);
}

static void Name(char (&destination)[256], const char* source)
{
    uint64_t i = 0;
    while (i < 255 && source[i])
    {
        destination[i] = source[i];
        ++i;
    }

    destination[i] = 0;
}

static void Info(HackosFileInfo& info, ActiveFile* file)
{
    info.size = file->GetSize();
    info.attributes = file->GetAttributes();
    Name(info.name, file->GetName());
}

static uint64_t UserInput()
{
    if (!OwnsTerminal(Caller()->pid))
    {
        return HACKOS_DENIED;
    }

    char ch;
    auto result = ReadKeyboard(ch);
    return result == KeyboardRead::Overflow    ? HACKOS_INPUT_OVERFLOW
           : result == KeyboardRead::Character ? (uint8_t)ch
                                               : 0;
}

static uint64_t UserConsole(uint64_t operation)
{
    if (!OwnsTerminal(Caller()->pid))
    {
        return HACKOS_DENIED;
    }

    if (operation == 0)
    {
        kprintf_backspace();
    }
    else if (operation == 1)
    {
        clrscr();
    }
    else
    {
        return HACKOS_ERROR;
    }

    return 0;
}

static uint64_t UserClock(HackosClock* destination)
{
    struct tm time;
    GetTimeRTC(&time);
    HackosClock value = {tick,
                         (uint8_t)time.second,
                         (uint8_t)time.minute,
                         (uint8_t)time.hour,
                         (uint8_t)time.day,
                         (uint8_t)time.month,
                         (uint8_t)time.year,
                         {0, 0}};
    return Output(destination, &value, sizeof(value)) ? 0 : HACKOS_ERROR;
}

static uint64_t UserProcessInfo(uint64_t index, HackosProcessInfo* destination)
{
    auto p = ready_queue;
    while (p && index)
    {
        p = p->next;
        --index;
    }

    if (!p)
    {
        return 0;
    }

    HackosProcessInfo info = {};
    info.pid = p->pid;
    info.state = p->state;
    info.uid = p->credentials.uid;
    info.gid = p->credentials.gid;
    info.parentPid = p->parentPid;
    Name(info.name, p->name);
    return Output(destination, &info, sizeof(info)) ? 1 : HACKOS_ERROR;
}

static uint64_t UserKill(uint64_t pid)
{
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->pid == pid)
        {
            if (!p->userSpace || p->systemShell || p == Caller() ||
                (!IsRoot() && Caller()->credentials.uid != p->credentials.uid))
            {
                return HACKOS_DENIED;
            }

            if (p->state == PROCESS_STATE_ZOMBIE)
            {
                return HACKOS_ERROR;
            }

            Kill(pid);
            return 0;
        }
    }

    return HACKOS_ERROR;
}

static uint64_t UserSpawn(const char* name)
{
    char path[256];
    if (!Path(name, path))
    {
        return HACKOS_ERROR;
    }

    unsigned count = 0;
    for (auto p = ready_queue; p; p = p->next)
    {
        ++count;
    }

    if (count >= 16)
    {
        return HACKOS_ERROR;
    }

    uint64_t pid = LoadUserProgramTask(path);
    return pid ? pid : HACKOS_ERROR;
}

static uint64_t UserSpawnArgs(const char* name, uint64_t argc, const char* const* argv,
                              const HackosCredentials* credentials = nullptr)
{
    char path[256], strings[HACKOS_ARG_BYTES];
    uint64_t pointers[HACKOS_MAX_ARGS];
    const char* arguments[HACKOS_MAX_ARGS];
    auto space = Caller()->userSpace;
    if (!Path(name, path) || !argc || argc > HACKOS_MAX_ARGS ||
        !space->CopyFromUser(pointers, (uint64_t)argv, argc * sizeof(uint64_t)))
    {
        return HACKOS_ERROR;
    }

    uint64_t used = 0;
    for (uint64_t i = 0; i < argc; ++i)
    {
        if (!space->CopyString(strings + used, pointers[i], sizeof(strings) - used))
        {
            return HACKOS_ERROR;
        }

        arguments[i] = strings + used;
        while (strings[used++])
        {
        }
    }

    unsigned count = 0;
    for (auto p = ready_queue; p; p = p->next)
    {
        ++count;
    }

    if (count >= 16)
    {
        return HACKOS_ERROR;
    }

    uint64_t pid = LoadUserProgramTask(path, false, argc, arguments, credentials);
    return pid ? pid : HACKOS_ERROR;
}

static void GiveTerminal(uint64_t pid)
{
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->pid == pid)
        {
            p->terminalReturnPid = Caller()->pid;
            SetTerminalOwner(pid);
            return;
        }
    }
}

static uint64_t UserSpawnForeground(const char* name, uint64_t argc, const char* const* argv)
{
    if (!OwnsTerminal(Caller()->pid))
    {
        return HACKOS_DENIED;
    }

    // Dispatcher holds a PreemptionGuard: the child cannot run before handoff.
    uint64_t pid = UserSpawnArgs(name, argc, argv);
    if (pid != HACKOS_ERROR)
    {
        GiveTerminal(pid);
    }

    return pid;
}

static uint64_t UserSpawnAs(const char* name, uint64_t argc, const char* const* argv, uint64_t uid, uint64_t flags)
{
    if (!IsRoot() || ((flags & HACKOS_SPAWN_FOREGROUND) && !OwnsTerminal(Caller()->pid)))
    {
        return HACKOS_DENIED;
    }

    const auto credentials = accounts.Credentials(uid);
    if (!credentials || (flags & ~HACKOS_SPAWN_FOREGROUND))
    {
        return HACKOS_ERROR;
    }

    uint64_t pid = UserSpawnArgs(name, argc, argv, credentials);
    if (pid != HACKOS_ERROR && (flags & HACKOS_SPAWN_FOREGROUND))
    {
        GiveTerminal(pid);
    }

    return pid;
}

static uint64_t UserIdentity(HackosCredentials* destination)
{
    return Output(destination, &Caller()->credentials, sizeof(*destination)) ? 0 : HACKOS_ERROR;
}

static uint64_t UserAccountInfo(uint64_t index, HackosUserInfo* destination)
{
    const auto value = accounts.User(index);
    return !value ? 0 : Output(destination, value, sizeof(*value)) ? 1 : HACKOS_ERROR;
}

static uint64_t UserGroupInfo(uint64_t index, HackosGroupInfo* destination)
{
    const auto value = accounts.Group(index);
    return !value ? 0 : Output(destination, value, sizeof(*value)) ? 1 : HACKOS_ERROR;
}

static uint64_t UserCreateAccount(const char* name, uint64_t gid)
{
    if (!IsRoot())
    {
        return HACKOS_DENIED;
    }

    char copy[HACKOS_ACCOUNT_NAME];
    return Caller()->userSpace->CopyString(copy, (uint64_t)name, sizeof(copy)) ? accounts.CreateUser(copy, gid)
                                                                               : HACKOS_ERROR;
}

static uint64_t UserCreateGroup(const char* name)
{
    if (!IsRoot())
    {
        return HACKOS_DENIED;
    }

    char copy[HACKOS_ACCOUNT_NAME];
    return Caller()->userSpace->CopyString(copy, (uint64_t)name, sizeof(copy)) ? accounts.CreateGroup(copy)
                                                                               : HACKOS_ERROR;
}

static uint64_t UserAddGroup(uint64_t uid, uint64_t gid)
{
    return IsRoot() ? accounts.AddGroup(uid, gid) : HACKOS_DENIED;
}

static uint64_t UserOpen(const char* name)
{
    char path[256];
    if (!Path(name, path) || nextHandle == ~0ULL - 2)
    {
        return HACKOS_ERROR;
    }

    for (unsigned i = 0; i < 8; ++i)
    {
        if (!Caller()->files[i])
        {
            auto file = filesystems[0]->OpenFile(path);
            if (!file)
            {
                return HACKOS_ERROR;
            }

            if (file->GetAttributes() & DIRECTORY)
            {
                filesystems[0]->CloseFile(file);
                return HACKOS_ERROR;
            }

            Caller()->files[i] = file;
            ++user_files_live;
            return Caller()->fileHandles[i] = ++nextHandle;
        }
    }

    return HACKOS_ERROR;
}

static int Handle(uint64_t handle)
{
    for (int i = 0; i < 8; ++i)
    {
        if (Caller()->files[i] && Caller()->fileHandles[i] == handle)
        {
            return i;
        }
    }

    return -1;
}

static uint64_t UserRead(uint64_t handle, void* destination, uint64_t length)
{
    int slot = Handle(handle);
    if (slot < 0 || length > 512 || !Caller()->userSpace->WritableRange((uint64_t)destination, length))
    {
        return HACKOS_ERROR;
    }

    auto file = Caller()->files[slot];
    uint64_t size = file->GetSize();
    uint64_t remaining = file->seek_position < size ? size - file->seek_position : 0;
    if (length > remaining)
    {
        length = remaining;
    }

    char buffer[512] = {};
    if (length && filesystems[0]->Read(file, buffer, length) != 0)
    {
        return HACKOS_ERROR;
    }

    return Output(destination, buffer, length) ? length : HACKOS_ERROR;
}

static uint64_t UserClose(uint64_t handle)
{
    int slot = Handle(handle);
    if (slot < 0)
    {
        return HACKOS_ERROR;
    }

    filesystems[0]->CloseFile(Caller()->files[slot]);
    --user_files_live;
    Caller()->files[slot] = nullptr;
    Caller()->fileHandles[slot] = 0;
    return 0;
}

static uint64_t UserStat(const char* name, HackosFileInfo* destination)
{
    char path[256];
    if (!Path(name, path) || !Caller()->userSpace->WritableRange((uint64_t)destination, sizeof(*destination)))
    {
        return HACKOS_ERROR;
    }

    HackosFileInfo info = {};
    if (!path[1])
    {
        info.attributes = DIRECTORY;
        info.name[0] = '~';
    }
    else
    {
        auto file = filesystems[0]->OpenFile(path);
        if (!file)
        {
            return HACKOS_ERROR;
        }

        Info(info, file);
        filesystems[0]->CloseFile(file);
    }

    return Output(destination, &info, sizeof(info)) ? 0 : HACKOS_ERROR;
}

static uint64_t UserReadDir(const char* name, uint64_t index, HackosFileInfo* destination)
{
    char path[256];
    if (!Path(name, path) || !Caller()->userSpace->WritableRange((uint64_t)destination, sizeof(*destination)))
    {
        return HACKOS_ERROR;
    }

    if (path[1])
    {
        auto directory = filesystems[0]->OpenFile(path);
        if (!directory)
        {
            return HACKOS_ERROR;
        }

        bool isDirectory = directory->GetAttributes() & DIRECTORY;
        filesystems[0]->CloseFile(directory);
        if (!isDirectory)
        {
            return HACKOS_ERROR;
        }
    }

    if (index >= filesystems[0]->GetDirectoryEntryCount(path))
    {
        return 0;
    }

    auto file = filesystems[0]->GetFile(path, index);
    if (!file)
    {
        return HACKOS_ERROR;
    }

    HackosFileInfo info = {};
    Info(info, file);
    filesystems[0]->CloseFile(file);
    return Output(destination, &info, sizeof(info)) ? 1 : HACKOS_ERROR;
}

static uint64_t UserGetCwd(char* destination, uint64_t capacity)
{
    uint64_t length = 0;
    while (Caller()->working_dir[length])
    {
        length++;
    }

    if (capacity <= length)
    {
        return HACKOS_ERROR;
    }

    return Output(destination, Caller()->working_dir, length + 1) ? 0 : HACKOS_ERROR;
}

static uint64_t UserChdir(const char* name)
{
    char path[256];
    if (!Path(name, path))
    {
        return HACKOS_ERROR;
    }

    if (path[1])
    {
        auto file = filesystems[0]->OpenFile(path);
        if (!file)
        {
            return HACKOS_ERROR;
        }

        bool directory = file->GetAttributes() & DIRECTORY;
        filesystems[0]->CloseFile(file);
        if (!directory)
        {
            return HACKOS_ERROR;
        }
    }

    Name(Caller()->working_dir, path);
    return 0;
}

static uint64_t UserPower(uint64_t operation)
{
    if (!IsRoot())
    {
        return HACKOS_DENIED;
    }

    if (operation == 0)
    {
        auto fadt = kInfo->rsdp ? (ACPI::FADT*)kInfo->rsdp->GetSystemTable("FACP") : nullptr;
        if (!fadt)
        {
            return HACKOS_ERROR;
        }

        kprintf("Stopping the CPU. Bye!\n");
        Display::SharedDisplay()->DrawBackbuffer();
        return fadt->Shutdown() ? 0 : HACKOS_ERROR;
    }
    else if (operation == 1)
    {
        kprintf("Restarting the CPU. Bye!\n");
        __outb(0x92, __inb(0x92) | 1);
        return 0;
    }

    return HACKOS_ERROR; // Unknown operation.
}

static void syscall_handler(Registers* r)
{
    // There is no ring-0 syscall ABI. Kernel code calls internal functions.
    if ((r->cs & 3) != 3 || !Caller() || !Caller()->userSpace)
    {
        r->rax = HACKOS_ERROR;
        return;
    }

    if (r->rax == HACKOS_SYS_exit)
    {
        ExitUserProcess(r->rdi);
    }

    if (r->rax == HACKOS_SYS_yield)
    {
        ScheduleIRQ();
        r->rax = 0;
        return;
    }

    if (r->rax == HACKOS_SYS_wait)
    {
        r->rax = WaitForChild(r->rdi, (int64_t*)r->rsi, r->rdx);
        return;
    }

    PreemptionGuard guard;
    switch (r->rax)
    {
    case HACKOS_SYS_user_write:
        r->rax = user_write((const char*)r->rdi, r->rsi);
        break;
    case HACKOS_SYS_input:
        r->rax = UserInput();
        break;
    case HACKOS_SYS_console:
        r->rax = UserConsole(r->rdi);
        break;
    case HACKOS_SYS_clock:
        r->rax = UserClock((HackosClock*)r->rdi);
        break;
    case HACKOS_SYS_process_info:
        r->rax = UserProcessInfo(r->rdi, (HackosProcessInfo*)r->rsi);
        break;
    case HACKOS_SYS_kill:
        r->rax = UserKill(r->rdi);
        break;
    case HACKOS_SYS_spawn:
        r->rax = UserSpawn((const char*)r->rdi);
        break;
    case HACKOS_SYS_spawn_args:
        r->rax = UserSpawnArgs((const char*)r->rdi, r->rsi, (const char* const*)r->rdx);
        break;
    case HACKOS_SYS_spawn_foreground:
        r->rax = UserSpawnForeground((const char*)r->rdi, r->rsi, (const char* const*)r->rdx);
        break;
    case HACKOS_SYS_open:
        r->rax = UserOpen((const char*)r->rdi);
        break;
    case HACKOS_SYS_read:
        r->rax = UserRead(r->rdi, (void*)r->rsi, r->rdx);
        break;
    case HACKOS_SYS_close:
        r->rax = UserClose(r->rdi);
        break;
    case HACKOS_SYS_stat:
        r->rax = UserStat((const char*)r->rdi, (HackosFileInfo*)r->rsi);
        break;
    case HACKOS_SYS_readdir:
        r->rax = UserReadDir((const char*)r->rdi, r->rsi, (HackosFileInfo*)r->rdx);
        break;
    case HACKOS_SYS_power:
        r->rax = UserPower(r->rdi);
        break;
    case HACKOS_SYS_getcwd:
        r->rax = UserGetCwd((char*)r->rdi, r->rsi);
        break;
    case HACKOS_SYS_chdir:
        r->rax = UserChdir((const char*)r->rdi);
        break;
    case HACKOS_SYS_identity:
        r->rax = UserIdentity((HackosCredentials*)r->rdi);
        break;
    case HACKOS_SYS_user_info:
        r->rax = UserAccountInfo(r->rdi, (HackosUserInfo*)r->rsi);
        break;
    case HACKOS_SYS_group_info:
        r->rax = UserGroupInfo(r->rdi, (HackosGroupInfo*)r->rsi);
        break;
    case HACKOS_SYS_user_create:
        r->rax = UserCreateAccount((const char*)r->rdi, r->rsi);
        break;
    case HACKOS_SYS_group_create:
        r->rax = UserCreateGroup((const char*)r->rdi);
        break;
    case HACKOS_SYS_user_add_group:
        r->rax = UserAddGroup(r->rdi, r->rsi);
        break;
    case HACKOS_SYS_spawn_as:
        r->rax = UserSpawnAs((const char*)r->rdi, r->rsi, (const char* const*)r->rdx, r->rcx, r->r8);
        break;
    case HACKOS_SYS_window_mode:
        r->rax = UserWindowMode(r->rdi, r->rsi);
        break;
    case HACKOS_SYS_window_state:
        r->rax = UserWindowState(r->rdi, (HackosWindowState*)r->rsi);
        break;
    case HACKOS_SYS_window_event:
        r->rax = UserWindowEvent(r->rdi, (HackosWindowEvent*)r->rsi);
        break;
    case HACKOS_SYS_window_create:
        r->rax = UserWindowCreate((const HackosWindowSpec*)r->rdi);
        break;
    case HACKOS_SYS_window_blit:
        r->rax = UserWindowBlit(r->rdi, (const HackosWindowRect*)r->rsi, (const uint32_t*)r->rdx);
        break;
    case HACKOS_SYS_window_poll:
        r->rax = UserWindowPoll(r->rdi);
        break;
    case HACKOS_SYS_window_close:
        r->rax = UserWindowClose(r->rdi);
        break;
    default:
        r->rax = HACKOS_ERROR;
        break;
    }
}

void InitialiseSyscalls()
{
    accounts.Initialise();
    RegisterInterruptHandler(0x80, syscall_handler);
}
