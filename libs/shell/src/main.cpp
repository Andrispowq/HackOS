#include "hackos/user.h"
#include "hackos/runtime/line_buffer.h"
#include "hackos/runtime/arguments.h"
#include "hackos/runtime/history.h"
#include "hackos/runtime/path.h"
using namespace User;
static char workingDirectory[256] = "~";
static CommandHistory history;

static void Prompt()
{
    syscall_getcwd(workingDirectory, sizeof(workingDirectory));
    HackosCredentials credentials;
    HackosUserInfo user;
    bool found = false;
    if (syscall_identity(&credentials) == 0)
    {
        for (uint64_t i = 0; syscall_user_info(i, &user) == 1; ++i)
        {
            if (user.credentials.uid == credentials.uid)
            {
                found = true;
                break;
            }
        }
    }

    Print(found ? user.name : "unknown");
    Print("@root:");
    Print(workingDirectory);
    Print("$ ");
}

static uint64_t backgroundTasks[16] = {};

static void CollectBackground()
{
    for (auto& pid : backgroundTasks)
    {
        if (pid && syscall_wait(pid, nullptr, HACKOS_WAIT_NOHANG) != 0)
        {
            pid = 0;
        }
    }
}

static bool Arguments(int argc, int expected, const char* usage)
{
    if (argc == expected)
    {
        return true;
    }

    Print("Usage: ");
    Print(usage);
    Print("\n");
    return false;
}

static void Launch(int argc, char** args, bool background, bool lookup = true)
{
    bool explicitPath = !lookup || args[0][0] == '~';
    for (const char* ch = args[0]; *ch; ++ch)
    {
        if (*ch == '/')
        {
            explicitPath = true;
        }
    }

    char path[256];
    if (!NormalizePath(args[0], explicitPath ? workingDirectory : "~/USR/BIN", path))
    {
        Print("Invalid or too long program path.\n");
        return;
    }

    HackosFileInfo info;
    if (syscall_stat(path, &info) != 0 || (info.attributes & HACKOS_DIRECTORY))
    {
        Print(explicitPath ? "Cannot load user program: " : "Unknown command: ");
        Print(args[0]);
        Print("\n");
        return;
    }

    args[0] = path;
    CollectBackground();
    uint64_t pid = background ? syscall_spawn_args(path, argc, args) : syscall_spawn_foreground(path, argc, args);
    if (pid == HACKOS_ERROR || pid == HACKOS_DENIED)
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

    if (!argc)
    {
        return;
    }

    const char* command = args[0];
    if (Equal(command, "user"))
    {
        if (argc < 2 || !*args[1])
        {
            Print("Usage: user <path> [args] [&]\n");
            return;
        }

        Launch(argc - 1, args + 1, background, false);
        return;
    }

    if (argc > HACKOS_MAX_ARGS)
    {
        Print("Invalid arguments (too many arguments).\n");
        return;
    }

    if (Equal(command, "cd") || Equal(command, "history"))
    {
        if (background)
        {
            Print("Shell built-ins cannot run in the background.\n");
            return;
        }

        if (Equal(command, "cd"))
        {
            if (!Arguments(argc, 2, "cd <path>"))
            {
                return;
            }

            if (!*args[1])
            {
                Print("Path must not be empty.\n");
                return;
            }

            if (syscall_chdir(args[1]) != 0)
            {
                Print("Directory not found.\n");
            }
        }
        else
        {
            if (!Arguments(argc, 1, "history"))
            {
                return;
            }

            for (uint64_t i = 0; i < history.Count(); ++i)
            {
                Number(i + 1);
                Print(" ");
                Print(history.At(i));
                Print("\n");
            }
        }

        return;
    }

    Launch(argc, args, background);
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
        if (value == HACKOS_DENIED)
        {
            Print("Shell does not own terminal input.\n");
            return 1;
        }

        if (!value)
        {
            syscall_yield();
            continue;
        }

        if (value == HACKOS_KEY_INTERRUPT)
        {
            line.Reset();
            history.ResetNavigation();
            discard = false;
            Print("^C\n");
            Prompt();
            continue;
        }

        if (!discard && (value == HACKOS_KEY_UP || value == HACKOS_KEY_DOWN))
        {
            const char* recalled = value == HACKOS_KEY_UP ? history.Up(line.Text()) : history.Down(line.Text());
            for (uint64_t i = 0; i < line.Length(); ++i)
            {
                syscall_console(0);
            }

            if (recalled != line.Text())
            {
                line.Set(recalled);
            }

            Print(line.Text());
            continue;
        }

        if (value == HACKOS_INPUT_OVERFLOW)
        {
            line.Reset();
            history.ResetNavigation();
            discard = true;
            Print("\nKeyboard queue overflow; press Enter to start a new command.\n");
            continue;
        }

        char ch = (char)value;
        if (discard)
        {
            if (ch == '\n')
            {
                discard = false;
                Prompt();
            }

            continue;
        }

        switch (line.Push(ch))
        {
        case LineBuffer::Echo:
            syscall_user_write(&ch, 1);
            break;
        case LineBuffer::Erase:
            syscall_console(0);
            break;
        case LineBuffer::Submit:
            Print("\n");
            history.Add(line.Text());
            Command(line.Text());
            line.Reset();
            Prompt();
            break;
        case LineBuffer::Overflow:
            Print("\nCommand too long (maximum 255 characters).\n");
            line.Reset();
            history.ResetNavigation();
            Prompt();
            break;
        default:
            break;
        }
    }
}
