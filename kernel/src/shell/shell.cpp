#include "shell.h"
#include "arch/x86_64/timer/rtc.h"
#include "arch/x86_64/timer/pit.h"
#include "arch/x86_64/interrupts/guard.h"
#include "fs/filesystem.h"
#include "proc/elf/elf.h"
#include "proc/tasking/process.h"
#include "proc/user/user.h"

static char working_dir[256];
static ActiveFile* openFile;
void InitialiseShell() { strcpy(working_dir, "~"); }

static bool PathFor(const char* name, char (&path)[256])
{
    if (!*name) return false;
    size_t prefix = *name == '~' ? 0 : strlen(working_dir) + 1;
    if (prefix + strlen((char*)name) >= sizeof(path)) { kprintf("Path too long.\n"); return false; }
    if (prefix) { strcpy(path, working_dir); path[prefix - 1] = '/'; }
    strcpy(path + prefix, name);
    return true;
}

static void Command(char* input)
{
    if (!*input) return;
    if (strcmp(input, "help") == 0)
    {
        kprintf("Commands: help, clear, print <text|tick|time|date>, ps, kill <pid>\n");
        kprintf("Files: ls, wd, cd <path>, open <path>, read, close, ./<program>\n");
        kprintf("User: user <path> (isolated ring 3). Power: shutdown, restart.\n");
    }
    else if (strcmp(input, "clear") == 0) clrscr();
    else if (strcmp(input, "ps") == 0) PrintAll();
    else if (strncmp(input, "kill ", 5) == 0)
    {
        uint64_t pid = 0;
        for (char* digit = input + 5; *digit; ++digit)
        {
            if (*digit < '0' || *digit > '9' || pid > (~0ULL - 9) / 10)
            { kprintf("Invalid PID.\n"); return; }
            pid = pid * 10 + *digit - '0';
        }
        if (pid == 1 || pid == GetRunningProcess()->pid)
            kprintf("Cannot stop the idle or shell task.\n");
        else if (!IsRunning(pid)) kprintf("No running task with that PID.\n");
        else { Kill(pid); kprintf("Task stopped.\n"); }
    }
    else if (strcmp(input, "wd") == 0) kprintf("%s\n", working_dir);
    else if (strncmp(input, "print ", 6) == 0)
    {
        char* message = input + 6;
        if (strcmp(message, "tick") == 0) kprintf("Ticks: %d\n", tick);
        else if (strcmp(message, "time") == 0 || strcmp(message, "date") == 0)
        {
            struct tm time;
            GetTimeRTC(&time);
            if (strcmp(message, "time") == 0) kprintf("Time: %x:%x:%x\n", time.hour, time.minute, time.second);
            else kprintf("Date: 20%x.%x.%x\n", time.year, time.month, time.day);
        }
        else kprintf("%s\n", message);
    }
    else if (strcmp(input, "print") == 0) kprintf("\n");
    else
    {
        Filesystem* drive = filesystems[0];
        if (!drive) { kprintf("No filesystem available.\n"); return; }
        char path[256];
        if (strcmp(input, "ls") == 0)
        {
            uint64_t count = drive->GetDirectoryEntryCount(working_dir);
            for (uint64_t i = 0; i < count; ++i)
            {
                ActiveFile* file = drive->GetFile(working_dir, i);
                if (!file) continue;
                kprintf("%s%s ", file->GetName(), file->GetAttributes() & DIRECTORY ? "/" : "");
                drive->CloseFile(file);
            }
            kprintf("\n");
        }
        else if (strncmp(input, "cd ", 3) == 0 && PathFor(input + 3, path))
        {
            if (strcmp(input + 3, "..") == 0)
            {
                size_t len = strlen(working_dir);
                while (len && working_dir[len] != '/') --len;
                if (len) working_dir[len] = 0;
                return;
            }
            if (strcmp(input + 3, ".") == 0) return;
            if (strcmp(path, "~") == 0) { strcpy(working_dir, "~"); return; }
            ActiveFile* file = drive->OpenFile(path);
            if (file)
            {
                if (file->GetAttributes() & DIRECTORY) strcpy(working_dir, path);
                else kprintf("Not a directory.\n");
                drive->CloseFile(file);
            }
            else kprintf("Directory not found.\n");
        }
        else if (strncmp(input, "open ", 5) == 0 && PathFor(input + 5, path))
        {
            if (openFile) drive->CloseFile(openFile);
            openFile = drive->OpenFile(path);
            if (openFile && (openFile->GetAttributes() & DIRECTORY))
            {
                drive->CloseFile(openFile);
                openFile = nullptr;
            }
            kprintf(openFile ? "File opened.\n" : "File not found.\n");
        }
        else if (strcmp(input, "close") == 0)
        {
            if (openFile) drive->CloseFile(openFile);
            openFile = nullptr;
        }
        else if (strcmp(input, "read") == 0)
        {
            if (!openFile) { kprintf("No open file.\n"); return; }
            uint64_t size = openFile->GetSize();
            size = openFile->seek_position < size ? size - openFile->seek_position : 0;
            if (size > 512) size = 512;
            char buffer[513] = {};
            if (size && drive->Read(openFile, buffer, size) != 0) kprintf("Read failed.\n");
            else kprintf("%s\n", buffer);
        }
        else if (strncmp(input, "user ", 5) == 0 && PathFor(input + 5, path)) LoadUserProgramTask(path);
        else if (strncmp(input, "./", 2) == 0 && PathFor(input + 2, path)) LoadProgramTask(path);
        else kprintf("Unknown command: %s\n", input);
    }
}

void shell_command(char* input)
{
    PreemptionGuard guard;
    Command(input);
    kprintf("root@root:%s$ ", working_dir);
}
