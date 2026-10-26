#include "hackos/user.h"
#include "hackos/runtime/line_buffer.h"
#include "hackos/runtime/arguments.h"
using namespace User;
static char workingDirectory[256] = "~";
static uint64_t openFile = HACKOS_ERROR;

static void Prompt()
{
    char prompt[272];
    Copy(prompt, "root@root:"); Copy(prompt + 10, workingDirectory);
    Copy(prompt + 10 + Length(workingDirectory), "$ ");
    Print(prompt);
}
static bool PathFor(const char* name, char (&path)[256])
{
    if (!*name) return false;
    uint64_t prefix = *name == '~' ? 0 : Length(workingDirectory) + 1;
    if (prefix + Length(name) >= sizeof(path)) { Print("Path too long.\n"); return false; }
    if (prefix) { Copy(path, workingDirectory); path[prefix - 1] = '/'; }
    Copy(path + prefix, name); return true;
}
static uint64_t backgroundTasks[16] = {};
static void CollectBackground()
{
    for (auto& pid : backgroundTasks)
        if (pid && syscall_wait(pid, nullptr, HACKOS_WAIT_NOHANG) != 0) pid = 0;
}
static void Launch(char* command)
{
    char* args[HACKOS_MAX_ARGS];
    bool background;
    int argc = ParseArguments(command, args, HACKOS_MAX_ARGS, background);
    if (argc <= 0) { Print("Invalid arguments (quotes, escapes, or too many arguments).\n"); return; }
    char path[256];
    if (!PathFor(args[0], path)) return;
    args[0] = path;
    CollectBackground();
    uint64_t pid = syscall_spawn_args(path, argc, args);
    if (pid == HACKOS_ERROR) { Print("Cannot load user program: "); Print(path); Print("\n"); return; }
    if (background)
    {
        for (auto& slot : backgroundTasks) if (!slot) { slot = pid; return; }
    }
    int64_t status;
    if (syscall_wait(pid, &status, 0) != pid) { Print("Wait failed.\n"); return; }
    if (status)
    {
        Print("Program exited with status ");
        if (status < 0) { Print("-"); Number((uint64_t)(-status)); }
        else Number(status);
        Print(".\n");
    }
}
static void Command(char* input)
{
    if (!*input) return;
    if (Starts(input, "user ")) { Launch(input + 5); return; }
    if (Starts(input, "./")) { Launch(input + 2); return; }
    if (Equal(input, "help"))
    {
        Print("Commands: help, clear, print <text|tick|time|date>, ps, kill <pid>\n");
        Print("Files: ls, wd, cd <path>, open <path>, read, close, ./<program>\n");
        Print("User: user <path> [args] (foreground; append & for background). Power: shutdown, restart.\n");
    }
    else if (Equal(input, "shutdown") || Equal(input, "restart"))
    {
        if (syscall_power(Equal(input, "shutdown") ? 0 : 1) != 0)
            Print("Power request failed.\n");
    }
    else if (Equal(input, "clear")) syscall_console(1);
    else if (Equal(input, "ps"))
    {
        HackosProcessInfo info;
        for (uint64_t i = 0; syscall_process_info(i, &info) == 1; ++i)
        {
            Print("Process: "); Print(info.name); Print(" ("); Number(info.pid);
            Print(info.state == 0 ? ") ALIVE\n" : info.state == 3 ? ") WAITING\n" : ") ZOMBIE\n");
        }
    }
    else if (Starts(input, "kill "))
    {
        uint64_t pid = 0;
        if (!input[5]) { Print("Invalid PID.\n"); return; }
        for (const char* digit = input + 5; *digit; ++digit)
        {
            if (*digit < '0' || *digit > '9' || pid > (~0ULL - 9) / 10)
            { Print("Invalid PID.\n"); return; }
            pid = pid * 10 + *digit - '0';
        }
        uint64_t result = syscall_kill(pid);
        Print(result == HACKOS_DENIED ? "Cannot stop the idle or shell task.\n" :
              result == HACKOS_ERROR ? "No running task with that PID.\n" : "Task stopped.\n");
    }
    else if (Equal(input, "wd")) { Print(workingDirectory); Print("\n"); }
    else if (Starts(input, "print "))
    {
        const char* text = input + 6;
        HackosClock time;
        if (Equal(text, "tick") || Equal(text, "time") || Equal(text, "date"))
        {
            if (syscall_clock(&time)) { Print("Clock unavailable.\n"); return; }
            if (Equal(text, "tick")) { Print("Ticks: "); Number(time.ticks); }
            else if (Equal(text, "time"))
            { Print("Time: "); Number(time.hour,16); Print(":"); Number(time.minute,16); Print(":"); Number(time.second,16); }
            else
            { Print("Date: 20"); Number(time.year,16); Print("."); Number(time.month,16); Print("."); Number(time.day,16); }
        }
        else Print(text);
        Print("\n");
    }
    else if (Equal(input, "print")) Print("\n");
    else
    {
        char path[256];
        if (Equal(input, "ls"))
        {
            HackosFileInfo info;
            for (uint64_t i = 0; syscall_readdir(workingDirectory, i, &info) == 1; ++i)
            { Print(info.name); Print(info.attributes & HACKOS_DIRECTORY ? "/ " : " "); }
            Print("\n");
        }
        else if (Starts(input, "cd ") && PathFor(input + 3, path))
        {
            if (Equal(input + 3, ".."))
            {
                uint64_t length = Length(workingDirectory);
                while (length && workingDirectory[length] != '/') --length;
                if (length) workingDirectory[length] = 0;
                return;
            }
            if (Equal(input + 3, ".")) return;
            HackosFileInfo info;
            if (syscall_stat(path, &info) != 0) Print("Directory not found.\n");
            else if (!(info.attributes & HACKOS_DIRECTORY)) Print("Not a directory.\n");
            else Copy(workingDirectory, path);
        }
        else if (Starts(input, "open ") && PathFor(input + 5, path))
        {
            if (openFile != HACKOS_ERROR) syscall_close(openFile);
            openFile = syscall_open(path);
            Print(openFile == HACKOS_ERROR ? "File not found.\n" : "File opened.\n");
        }
        else if (Equal(input, "close"))
        {
            if (openFile != HACKOS_ERROR) syscall_close(openFile);
            openFile = HACKOS_ERROR;
        }
        else if (Equal(input, "read"))
        {
            if (openFile == HACKOS_ERROR) { Print("No open file.\n"); return; }
            char buffer[512];
            uint64_t length = syscall_read(openFile, buffer, sizeof(buffer));
            if (length == HACKOS_ERROR) Print("Read failed.\n");
            else { syscall_user_write(buffer, length); Print("\n"); }
        }
        else { Print("Unknown command: "); Print(input); Print("\n"); }
    }
}
int main()
{
    Print("\nHackOS ring-3 shell. Type 'help' for commands.\n");
    Prompt();
    LineBuffer line;
    bool discard = false;
    for (;;)
    {
        CollectBackground();
        uint64_t value = syscall_input();
        if (value == HACKOS_DENIED) { Print("Only the boot shell owns terminal input.\n"); return 1; }
        if (!value) { syscall_yield(); continue; }
        if (value == HACKOS_INPUT_OVERFLOW)
        {
            line.Reset(); discard = true;
            Print("\nKeyboard queue overflow; press Enter to start a new command.\n");
            continue;
        }
        char ch = (char)value;
        if (discard) { if (ch == '\n') { discard = false; Prompt(); } continue; }
        switch (line.Push(ch))
        {
        case LineBuffer::Echo: syscall_user_write(&ch, 1); break;
        case LineBuffer::Erase: syscall_console(0); break;
        case LineBuffer::Submit: Print("\n"); Command(line.Text()); line.Reset(); Prompt(); break;
        case LineBuffer::Overflow:
            Print("\nCommand too long (maximum 255 characters).\n"); line.Reset(); Prompt(); break;
        default: break;
        }
    }
}
