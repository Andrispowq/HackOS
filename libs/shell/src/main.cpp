#include "hackos/user.h"
#include "hackos/runtime/line_buffer.h"
#include "hackos/runtime/arguments.h"
#include "hackos/runtime/history.h"
using namespace User;
static char workingDirectory[256] = "~";
static CommandHistory history;
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

static bool Arguments(int argc, int expected, const char* usage)
{
    if (argc == expected) return true;

    Print("Usage: ");
    Print(usage);
    Print("\n");
    return false;
}

static void Launch(int argc, char** args, bool background)
{
    char path[256];
    const char* name = Starts(args[0], "./") ? args[0] + 2 : args[0];
    if (!PathFor(name, path)) return;

    args[0] = path;
    CollectBackground();
    uint64_t pid = background ? syscall_spawn_args(path, argc, args) : syscall_spawn_foreground(path, argc, args);
    if (pid == HACKOS_ERROR)
    {
        Print("Cannot load user program: ");
        Print(path);
        Print("\n");
        return;
    }

    if (background)
    {
        for (auto& slot : backgroundTasks)
        {
            if (!slot)
            {
                slot = pid;
                return;
            }
        }
    }

    int64_t status;
    if (syscall_wait(pid, &status, 0) != pid)
    {
        Print("Wait failed.\n");
        return;
    }

    if (status == 130)
    {
        Print("^C\n");
        return;
    }

    if (status)
    {
        Print("Program exited with status ");
        if (status < 0)
        {
            Print("-");
            Number((uint64_t)(-status));
        }
        else
        {
            Number(status);
        }

        Print(".\n");
    }
}

static void Command(char* input)
{
    // The optional user prefix is not part of the program argv limit.
    char* args[HACKOS_MAX_ARGS + 1];
    bool background;
    int argc = ParseArguments(input, args, HACKOS_MAX_ARGS + 1, background);
    if (argc < 0)
    {
        Print("Invalid arguments (quotes, escapes, or too many arguments).\n");
        return;
    }

    if (!argc) return;

    const char* command = args[0];
    if (Equal(command, "user"))
    {
        if (argc < 2 || !*args[1])
        {
            Print("Usage: user <path> [args] [&]\n");
            return;
        }

        Launch(argc - 1, args + 1, background);
        return;
    }

    if (argc > HACKOS_MAX_ARGS)
    {
        Print("Invalid arguments (too many arguments).\n");
        return;
    }

    if (Starts(command, "./"))
    {
        Launch(argc, args, background);
        return;
    }

    if (background)
    {
        Print("Background execution requires user <path> or ./<program>.\n");
        return;
    }

    if (Equal(command, "help"))
    {
        if (!Arguments(argc, 1, "help")) return;

        Print("Commands: help, history, clear, print [text|tick|time|date], ps [aux], kill <pid>\n");
        Print("Files: ls, wd, cd <path>, open <path>, read, close, ./<program>\n");
        Print("User: user <path> [args] (foreground; append & for background). Power: shutdown, restart.\n");
    }
    else if (Equal(command, "shutdown") || Equal(command, "restart"))
    {
        if (!Arguments(argc, 1, command)) return;

        if (syscall_power(Equal(command, "shutdown") ? 0 : 1) != 0)
        {
            Print("Power request failed.\n");
        }
    }
    else if (Equal(command, "history"))
    {
        if (!Arguments(argc, 1, "history")) return;

        for (uint64_t i = 0; i < history.Count(); ++i)
        {
            Number(i + 1);
            Print(" ");
            Print(history.At(i));
            Print("\n");
        }
    }
    else if (Equal(command, "clear"))
    {
        if (!Arguments(argc, 1, "clear")) return;

        syscall_console(1);
    }
    else if (Equal(command, "ps"))
    {
        // All processes are already listed. Accept the familiar spelling;
        // ownership and CPU/memory accounting are not part of this ABI yet.
        if (argc != 1 && !(argc == 2 && Equal(args[1], "aux")))
        {
            Print("Usage: ps [aux]\n");
            return;
        }

        HackosProcessInfo info;
        for (uint64_t i = 0; syscall_process_info(i, &info) == 1; ++i)
        {
            Print("Process: ");
            Print(info.name);
            Print(" (");
            Number(info.pid);
            Print(info.state == 0 ? ") ALIVE\n" : info.state == 3 ? ") WAITING\n" : ") ZOMBIE\n");
        }
    }
    else if (Equal(command, "kill"))
    {
        if (!Arguments(argc, 2, "kill <pid>")) return;

        uint64_t pid = 0;
        if (!*args[1])
        {
            Print("Invalid PID.\n");
            return;
        }

        for (const char* digit = args[1]; *digit; ++digit)
        {
            if (*digit < '0' || *digit > '9' || pid > (~0ULL - (*digit - '0')) / 10)
            {
                Print("Invalid PID.\n");
                return;
            }

            pid = pid * 10 + *digit - '0';
        }

        uint64_t result = syscall_kill(pid);
        Print(result == HACKOS_DENIED ? "Cannot stop the idle or shell task.\n" :
              result == HACKOS_ERROR ? "No running task with that PID.\n" : "Task stopped.\n");
    }
    else if (Equal(command, "wd"))
    {
        if (!Arguments(argc, 1, "wd")) return;

        Print(workingDirectory);
        Print("\n");
    }
    else if (Equal(command, "print"))
    {
        const char* text = argc == 2 ? args[1] : "";
        if (Equal(text, "tick") || Equal(text, "time") || Equal(text, "date"))
        {
            HackosClock time;
            if (syscall_clock(&time))
            {
                Print("Clock unavailable.\n");
                return;
            }

            if (Equal(text, "tick"))
            {
                Print("Ticks: ");
                Number(time.ticks);
            }
            else if (Equal(text, "time"))
            {
                Print("Time: ");
                Number(time.hour, 16);
                Print(":");
                Number(time.minute, 16);
                Print(":");
                Number(time.second, 16);
            }
            else
            {
                Print("Date: 20");
                Number(time.year, 16);
                Print(".");
                Number(time.month, 16);
                Print(".");
                Number(time.day, 16);
            }
        }
        else
        {
            for (int i = 1; i < argc; ++i)
            {
                if (i > 1) Print(" ");
                Print(args[i]);
            }
        }

        Print("\n");
    }
    else if (Equal(command, "ls"))
    {
        if (!Arguments(argc, 1, "ls")) return;

        HackosFileInfo info;
        for (uint64_t i = 0; syscall_readdir(workingDirectory, i, &info) == 1; ++i)
        {
            Print(info.name);
            Print(info.attributes & HACKOS_DIRECTORY ? "/ " : " ");
        }

        Print("\n");
    }
    else if (Equal(command, "cd") || Equal(command, "open"))
    {
        bool directory = Equal(command, "cd");
        const char* usage = directory ? "cd <path>" : "open <path>";
        if (!Arguments(argc, 2, usage)) return;
        if (!*args[1])
        {
            Print("Path must not be empty.\n");
            return;
        }

        char path[256];
        if (!PathFor(args[1], path)) return;

        if (directory)
        {
            if (Equal(args[1], ".."))
            {
                uint64_t length = Length(workingDirectory);
                while (length && workingDirectory[length] != '/')
                {
                    --length;
                }

                if (length) workingDirectory[length] = 0;
                return;
            }

            if (Equal(args[1], ".")) return;

            HackosFileInfo info;
            if (syscall_stat(path, &info) != 0)
            {
                Print("Directory not found.\n");
            }
            else if (!(info.attributes & HACKOS_DIRECTORY))
            {
                Print("Not a directory.\n");
            }
            else
            {
                Copy(workingDirectory, path);
            }
        }
        else
        {
            if (openFile != HACKOS_ERROR) syscall_close(openFile);
            openFile = syscall_open(path);
            Print(openFile == HACKOS_ERROR ? "File not found.\n" : "File opened.\n");
        }
    }
    else if (Equal(command, "close"))
    {
        if (!Arguments(argc, 1, "close")) return;

        if (openFile != HACKOS_ERROR) syscall_close(openFile);
        openFile = HACKOS_ERROR;
    }
    else if (Equal(command, "read"))
    {
        if (!Arguments(argc, 1, "read")) return;
        if (openFile == HACKOS_ERROR)
        {
            Print("No open file.\n");
            return;
        }

        char buffer[512];
        uint64_t length = syscall_read(openFile, buffer, sizeof(buffer));
        if (length == HACKOS_ERROR)
        {
            Print("Read failed.\n");
        }
        else
        {
            syscall_user_write(buffer, length);
            Print("\n");
        }
    }
    else
    {
        Print("Unknown command: ");
        Print(command);
        Print("\n");
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
        if (value == HACKOS_DENIED) { Print("Shell does not own terminal input.\n"); return 1; }
        if (!value) { syscall_yield(); continue; }
        if (value == HACKOS_KEY_INTERRUPT)
        {
            line.Reset(); history.ResetNavigation(); discard = false;
            Print("^C\n"); Prompt(); continue;
        }
        if (!discard && (value == HACKOS_KEY_UP || value == HACKOS_KEY_DOWN))
        {
            const char* recalled = value == HACKOS_KEY_UP ? history.Up(line.Text()) : history.Down(line.Text());
            for (uint64_t i = 0; i < line.Length(); ++i) syscall_console(0);
            if (recalled != line.Text()) line.Set(recalled);
            Print(line.Text()); continue;
        }
        if (value == HACKOS_INPUT_OVERFLOW)
        {
            line.Reset(); history.ResetNavigation(); discard = true;
            Print("\nKeyboard queue overflow; press Enter to start a new command.\n");
            continue;
        }
        char ch = (char)value;
        if (discard) { if (ch == '\n') { discard = false; Prompt(); } continue; }
        switch (line.Push(ch))
        {
        case LineBuffer::Echo: syscall_user_write(&ch, 1); break;
        case LineBuffer::Erase: syscall_console(0); break;
        case LineBuffer::Submit: Print("\n"); history.Add(line.Text()); Command(line.Text()); line.Reset(); Prompt(); break;
        case LineBuffer::Overflow:
            Print("\nCommand too long (maximum 255 characters).\n"); line.Reset(); history.ResetNavigation(); Prompt(); break;
        default: break;
        }
    }
}
