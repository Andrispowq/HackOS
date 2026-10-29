#include "user.h"
#include "streams.h"
#include "drivers/serial/serial.h"
#include "fs/permissions.h"
#include "fs/filesystem.h"
#include "arch/x86_64/ports.h"
#include "hackos/abi.h"
#include "lib/string.h"
#include "address_space.h"
#include "proc/elf/elf.h"
#include "proc/tasking/process.h"
#include "arch/x86_64/interrupts/guard.h"
#include "lib/memory.h"
#include "lib/stdio.h"
#include "drivers/screen/screen.h"

// Lifecycle diagnostics remain available to QEMU/debug tooling without
// cluttering the graphical terminal for every utility command.
static void DebugText(const char* text)
{
    while (*text)
    {
        SerialCharacter(*text);
        __outb(0xe9, *text++);
    }
}

static void DebugNumber(uint64_t value)
{
    char digits[20];
    unsigned count = 0;
    do
    {
        digits[count++] = '0' + value % 10;
        value /= 10;
    } while (value);

    while (count)
    {
        SerialCharacter(digits[--count]);
        __outb(0xe9, digits[count]);
    }
}

uint64_t LoadUserProgramTask(const char* path, bool systemShell, uint64_t argc, const char* const* argv,
                             const HackosCredentials* credentials)
{
    PreemptionGuard guard;
    extern Process* ready_queue;
    unsigned processCount = 0;
    for (auto process = ready_queue; process; process = process->next)
    {
        ++processCount;
    }
    if (processCount >= HACKOS_PROCESS_LIMIT)
    {
        return 0;
    }
    auto parent = GetRunningProcess();
    ChildResult* result = nullptr;
    if (parent && parent->userSpace)
    {
        for (auto& child : parent->children)
        {
            if (!child.pid)
            {
                result = &child;
                break;
            }
        }

        if (!result)
        {
            return 0;
        }
    }

    const char* defaultArgs[] = {path};
    if (!argv)
    {
        argc = 1;
        argv = defaultArgs;
    }

    if (!argc || argc > HACKOS_MAX_ARGS)
    {
        return 0;
    }

    uint64_t bytes = 0;
    for (uint64_t i = 0; i < argc; ++i)
    {
        bytes += strlen((char*)argv[i]) + 1;
        if (bytes > HACKOS_ARG_BYTES)
        {
            return 0;
        }
    }

    const auto& identity = credentials ? *credentials : parent ? parent->credentials : HackosCredentials{};
    if (!PathAccess(path, identity, 5))
    {
        return 0;
    }

    extern Process* ready_queue;
    for (auto process = ready_queue; process; process = process->next)
    {
        for (auto file : process->files)
        {
            if (file && (file->accessMode & HACKOS_OPEN_WRITE) && !strcmp((char*)file->GetPath(), (char*)path))
            {
                return 0;
            }
        }
    }

    if (StreamPathBusy(path, true))
    {
        return 0;
    }

    uint64_t address;
    auto header = LoadProgram(path, &address, false);
    if (!header)
    {
        kprintf("Cannot load user program: %s\n", path);
        return false;
    }

    // Validate policy before allocating or mapping any destination pages.
    auto segments = (Elf64_Phdr*)(address + header->e_phoff);
    uint64_t pages = 4; // user stack
    bool valid = true;
    for (uint64_t i = 0; i < header->e_phnum && valid; ++i)
    {
        const auto& s = segments[i];
        if (s.p_type == PT_INTERP || s.p_type == PT_DYNAMIC || s.p_type == PT_TLS)
        {
            valid = false;
            break;
        }

        if (s.p_type != PT_LOAD || !s.p_memsz)
        {
            continue;
        }

        if (s.p_vaddr < USER_BASE || s.p_vaddr >= USER_IMAGE_END || s.p_memsz > USER_IMAGE_END - s.p_vaddr ||
            !(s.p_flags & 4) || (s.p_flags & ~7U) || (s.p_flags & 3) == 3)
        {
            valid = false;
            break;
        }

        uint64_t first = s.p_vaddr & ~4095ULL;
        uint64_t end = (s.p_vaddr + s.p_memsz + 4095) & ~4095ULL;
        pages += (end - first) / 4096;
        if (pages > USER_MAX_PAGES)
        {
            valid = false;
            break;
        }

        // Reject page-sharing segments instead of accidentally merging W/X.
        for (uint64_t j = 0; j < i; ++j)
        {
            if (segments[j].p_type == PT_LOAD && segments[j].p_memsz &&
                first < ((segments[j].p_vaddr + segments[j].p_memsz + 4095) & ~4095ULL) &&
                (segments[j].p_vaddr & ~4095ULL) < end)
            {
                valid = false;
            }
        }
    }

    auto space = new UserAddressSpace();
    valid = valid && space->Initialise();
    for (uint64_t i = 0; i < header->e_phnum && valid; ++i)
    {
        const auto& s = segments[i];
        if (s.p_type != PT_LOAD || !s.p_memsz)
        {
            continue;
        }

        uint64_t end = s.p_vaddr + s.p_memsz;
        for (uint64_t page = s.p_vaddr & ~4095ULL; page < end; page += 4096)
        {
            auto destination = (uint8_t*)space->AddPage(page, s.p_flags & 2, s.p_flags & 1);
            if (!destination)
            {
                valid = false;
                break;
            }

            uint64_t start = page > s.p_vaddr ? page : s.p_vaddr;
            uint64_t fileEnd = s.p_vaddr + s.p_filesz;
            uint64_t copyEnd = page + 4096 < fileEnd ? page + 4096 : fileEnd;
            if (copyEnd > start)
            {
                memcpy(destination + start - page, (void*)(address + s.p_offset + start - s.p_vaddr), copyEnd - start);
            }

            // AllocatePage has already zeroed the BSS, padding and stack.
        }
    }

    for (uint64_t page = USER_STACK_BOTTOM; page < USER_END && valid; page += 4096)
    {
        valid = space->AddPage(page, true, false) != nullptr;
    }

    if (!valid)
    {
        delete space;
        kfree(header);
        kprintf("Rejected user program: %s\n", path);
        return false;
    }

    // The argument strings and NULL-terminated pointer vector live in the
    // child's own writable stack pages, above the initial call stack.
    uint64_t cursor = USER_END;
    uint64_t userArgv[HACKOS_MAX_ARGS + 1] = {};
    for (uint64_t i = argc; i > 0; --i)
    {
        uint64_t length = strlen((char*)argv[i - 1]) + 1;
        cursor -= length;
        space->CopyToUser(cursor, argv[i - 1], length);
        userArgv[i - 1] = cursor;
    }

    cursor = (cursor & ~7ULL) - (argc + 1) * sizeof(uint64_t);
    space->CopyToUser(cursor, userArgv, (argc + 1) * sizeof(uint64_t));
    auto process = new Process(path, (void*)header->e_entry);
    InheritStreams(process, parent);
    process->systemShell = systemShell;
    process->processGroup = !parent || parent->systemShell ? process->pid : parent->processGroup;
    process->jobOwnerPid = parent && parent->systemShell ? parent->pid : parent ? parent->jobOwnerPid : 0;
    process->sessionId = parent ? parent->sessionId : 0;
    process->terminalId = parent ? parent->terminalId : 0;
    process->credentials = credentials ? *credentials : parent ? parent->credentials : HackosCredentials{};
    if (parent && parent->userSpace)
    {
        strcpy(process->working_dir, parent->working_dir);
    }

    if (systemShell)
    {
        SetTerminalOwner(process->pid);
    }

    if (result)
    {
        process->parentPid = parent->pid;
        result->pid = process->pid;
        result->job = parent->systemShell && !parent->authenticationAgent;
    }

    process->userSpace = space;
    process->pageTable = &space->directory;
    auto frame = (Registers*)process->rsp;
    frame->rip = header->e_entry;
    frame->cs = 0x1b;
    frame->ds = frame->ss = 0x23;
    frame->rsp = (cursor & ~15ULL) - 8; // SysV entry alignment; zero return address
    frame->rdi = argc;
    frame->rsi = cursor;
    frame->rflags = 0x202; // IF set, IOPL=0, no NT/VM/DF/TF
    kfree(header);
    AddProcess(process);
    DebugText("Started user process ");
    DebugNumber(process->pid);
    DebugText(": ");
    DebugText(path);
    DebugText("\n");
    return process->pid;
}

uint64_t user_write(const char* buffer, uint64_t length)
{
    return UserFdWrite(HACKOS_STDOUT, buffer, length, HACKOS_WAIT_FOREVER);
}

[[noreturn]] void ExitUserProcess(uint64_t code)
{
    auto p = GetRunningProcess();
    DebugText("User process ");
    DebugNumber(p->pid);
    DebugText(" exited ");
    DebugNumber(code);
    DebugText("; preemptions ");
    DebugNumber(p->userPreemptions);
    DebugText("\n");
    p->exitStatus = (int32_t)code;
    _Kill();
}

[[noreturn]] void FaultUserProcess(Registers* frame)
{
    auto p = GetRunningProcess();
    kprintf("User process %d fault %d error %x rip %x\n", p->pid, frame->int_no, frame->err_code, frame->rip);
    p->exitStatus = 128 + frame->int_no;
    _Kill();
}
