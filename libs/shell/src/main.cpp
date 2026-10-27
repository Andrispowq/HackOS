#include "hackos/user.h"
#include "hackos/stdio.h"
#include "hackos/runtime/line_buffer.h"
#include "hackos/runtime/pipeline.h"
#include "hackos/runtime/history.h"
#include "hackos/runtime/path.h"
#include "hackos/runtime/completion.h"
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

    char prompt[320];
    snprintf(prompt, sizeof(prompt), "%s@root:%s$ ", found ? user.name : "unknown", workingDirectory);
    Print(prompt);
}

struct Job
{
    uint64_t group = 0;
    char name[256] = {};
};

static Job jobs[16];

static void JobLine(const Job& job, const char* state)
{
    char message[320];
    snprintf(message, sizeof(message), "[%lu] %s %s\n", job.group, state, job.name);
    Print(message);
}

static void CollectBackground()
{
    for (auto& job : jobs)
    {
        if (job.group && syscall_job_control(job.group, HACKOS_JOB_QUERY) == 0)
        {
            syscall_job_wait(job.group, nullptr, HACKOS_WAIT_NOHANG);
            JobLine(job, "Done");
            job = {};
        }
    }
}

static bool WaitJob(Job& job)
{
    int64_t status = 0;
    uint64_t result = syscall_job_wait(job.group, &status, 0);
    if (result == HACKOS_JOB_INTERRUPTED)
    {
        Print("^C\n");
        return false;
    }

    if (result == HACKOS_JOB_STOPPED)
    {
        Print("^Z\n");
        JobLine(job, "Stopped");
        return false;
    }

    if (result != job.group)
    {
        Error("Wait failed.\n");
        return false;
    }

    job = {};
    if (status == 130)
    {
        Print("^C\n");
    }
    else if (status)
    {
        char message[80];
        snprintf(message, sizeof(message), "Program exited with status %ld.\n", status);
        Error(message);
    }

    return true;
}

static Job* FindJob(const char* name)
{
    uint64_t group = 0;
    if (name)
    {
        if (*name == '%')
        {
            ++name;
        }

        if (!*name)
        {
            return nullptr;
        }

        while (*name)
        {
            if (*name < '0' || *name > '9' || group > (~0ULL - (*name - '0')) / 10)
            {
                return nullptr;
            }

            group = group * 10 + *name++ - '0';
        }
    }

    Job* found = nullptr;
    for (auto& job : jobs)
    {
        if (job.group && (name ? job.group == group : !found || job.group > found->group))
        {
            found = &job;
        }
    }

    return found;
}

static bool Arguments(int argc, int expected, const char* usage)
{
    if (argc == expected)
    {
        return true;
    }

    Error("Usage: ");
    Error(usage);
    Error("\n");
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
        Error("Invalid or too long program path.\n");
        return;
    }

    HackosFileInfo info;
    if (syscall_stat(path, &info) != 0 || (info.attributes & HACKOS_DIRECTORY))
    {
        Error(explicitPath ? "Cannot load user program: " : "Unknown command: ");
        Error(args[0]);
        Error("\n");
        return;
    }

    args[0] = path;
    CollectBackground();
    Job* job = nullptr;
    for (auto& slot : jobs)
    {
        if (!slot.group)
        {
            job = &slot;
            break;
        }
    }

    if (!job)
    {
        Error("Job table full; wait for a job first.\n");
        return;
    }

    uint64_t pid = syscall_spawn_job(path, argc, args, background ? 0 : HACKOS_SPAWN_FOREGROUND);
    if (pid == HACKOS_ERROR || pid == HACKOS_DENIED)
    {
        Error("Cannot load user program: ");
        Error(path);
        Error("\n");
        return;
    }

    job->group = pid;
    Copy(job->name, path);
    if (background)
    {
        JobLine(*job, "Running");
    }
    else
    {
        WaitJob(*job);
    }
}

static void Builtin(int argc, char** args, bool background)
{
    const char* command = args[0];
    if (Equal(command, "user"))
    {
        if (argc < 2 || !*args[1])
        {
            Error("Usage: user <path> [args] [&]\n");
            return;
        }

        Launch(argc - 1, args + 1, background, false);
        return;
    }

    if (argc > HACKOS_MAX_ARGS)
    {
        Error("Invalid arguments (too many arguments).\n");
        return;
    }

    bool groupKill = Equal(command, "kill") && argc == 2 && args[1][0] == '%';
    if (Equal(command, "jobs") || Equal(command, "fg") || Equal(command, "bg") || Equal(command, "wait") || groupKill)
    {
        if (background || argc > 2 || (Equal(command, "jobs") && argc != 1))
        {
            Error("Usage: jobs | fg [job] | bg [job] | wait [job] | kill %job\n");
            return;
        }

        CollectBackground();
        if (Equal(command, "jobs"))
        {
            for (const auto& job : jobs)
            {
                if (job.group)
                {
                    JobLine(job, syscall_job_control(job.group, HACKOS_JOB_QUERY) == 2 ? "Stopped" : "Running");
                }
            }

            return;
        }

        if (Equal(command, "wait") && argc == 1)
        {
            for (auto& job : jobs)
            {
                if (job.group && !WaitJob(job))
                {
                    break;
                }
            }

            return;
        }

        auto job = FindJob(argc == 2 ? args[1] : nullptr);
        if (!job)
        {
            Error("No such job.\n");
            return;
        }

        if (Equal(command, "wait"))
        {
            WaitJob(*job);
            return;
        }

        uint64_t operation = groupKill              ? HACKOS_JOB_TERMINATE
                             : Equal(command, "bg") ? HACKOS_JOB_BACKGROUND
                                                    : HACKOS_JOB_FOREGROUND;
        if (syscall_job_control(job->group, operation) != 0)
        {
            Error("Job operation failed.\n");
        }
        else if (operation == HACKOS_JOB_BACKGROUND)
        {
            JobLine(*job, "Running");
        }
        else
        {
            WaitJob(*job);
        }

        return;
    }

    if (Equal(command, "logout"))
    {
        if (background || !Arguments(argc, 1, "logout"))
        {
            return;
        }

        syscall_exit(0);
    }

    if (Equal(command, "cd") || Equal(command, "history"))
    {
        if (background)
        {
            Error("Shell built-ins cannot run in the background.\n");
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
                Error("Path must not be empty.\n");
                return;
            }

            if (syscall_chdir(args[1]) != 0)
            {
                Error("Directory not found.\n");
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

static bool IsBuiltin(const ShellStage& stage)
{
    const char* name = stage.argv[0];
    return Equal(name, "cd") || Equal(name, "history") || Equal(name, "logout") || Equal(name, "jobs") ||
           Equal(name, "fg") || Equal(name, "bg") || Equal(name, "wait") ||
           (Equal(name, "kill") && stage.argc == 2 && stage.argv[1][0] == '%');
}

static bool Redirect(const ShellStage& stage, uint64_t (&streams)[3], uint64_t (&opened)[8], unsigned& count)
{
    for (unsigned i = 0; i < stage.redirectCount; ++i)
    {
        const auto& redirect = stage.redirects[i];
        if (redirect.source >= 0)
        {
            streams[redirect.target] = streams[redirect.source];
        }
        else
        {
            uint64_t fd = syscall_fd_open(redirect.path, redirect.mode);
            if (fd >= HACKOS_FD_LIMIT)
            {
                Error("Cannot open redirection: ");
                Error(redirect.path);
                Error("\n");
                return false;
            }

            opened[count++] = fd;
            streams[redirect.target] = fd;
        }
    }

    return true;
}

static void Command(char* input)
{
    ShellPipeline pipeline;
    if (!ParsePipeline(input, pipeline))
    {
        Error("Invalid arguments (quotes, escapes, operators, or too many arguments).\n");
        return;
    }

    if (!pipeline.count)
    {
        return;
    }

    // Preserve the existing simple launch path and its diagnostics.
    if (pipeline.count == 1 && !pipeline.stages[0].redirectCount)
    {
        auto& stage = pipeline.stages[0];
        Builtin(stage.argc, stage.argv, pipeline.background);
        return;
    }

    for (unsigned i = 0; i < pipeline.count; ++i)
    {
        if (IsBuiltin(pipeline.stages[i]) && (pipeline.count != 1 || pipeline.background))
        {
            Error("Shell built-ins require a single foreground command.\n");
            return;
        }
    }

    if (IsBuiltin(pipeline.stages[0]))
    {
        // Saved descriptors also give ordered dup redirections stable sources.
        uint64_t streams[3] = {13, 14, 15}, opened[8];
        unsigned count = 0;
        bool saved = true;
        for (unsigned i = 0; i < 3; ++i)
        {
            saved = syscall_dup2(i, 13 + i) == 13 + i && saved;
        }

        bool ready = saved && Redirect(pipeline.stages[0], streams, opened, count);
        if (ready)
        {
            for (unsigned i = 0; i < 3; ++i)
            {
                syscall_dup2(streams[i], i);
            }
        }

        for (unsigned i = 0; i < count; ++i)
        {
            syscall_fd_close(opened[i]);
        }

        if (ready)
        {
            auto& stage = pipeline.stages[0];
            Builtin(stage.argc, stage.argv, false);
        }

        for (unsigned i = 0; i < 3; ++i)
        {
            syscall_dup2(13 + i, i);
            syscall_fd_close(13 + i);
        }

        return;
    }

    char paths[4][256];
    for (unsigned i = 0; i < pipeline.count; ++i)
    {
        auto& stage = pipeline.stages[i];
        bool explicitPath = Equal(stage.argv[0], "user");
        if (explicitPath)
        {
            --stage.argc;
            for (unsigned j = 0; j < stage.argc; ++j)
            {
                stage.argv[j] = stage.argv[j + 1];
            }
        }

        if (!stage.argc || stage.argc > HACKOS_MAX_ARGS || !*stage.argv[0])
        {
            Error("Invalid program arguments.\n");
            return;
        }

        for (const char* ch = stage.argv[0]; *ch; ++ch)
        {
            explicitPath = explicitPath || *ch == '/' || *ch == '~';
        }

        HackosFileInfo info;
        if (!NormalizePath(stage.argv[0], explicitPath ? workingDirectory : "~/USR/BIN", paths[i]) ||
            syscall_stat(paths[i], &info) != 0 || (info.attributes & HACKOS_DIRECTORY))
        {
            Error("Cannot load user program: ");
            Error(stage.argv[0]);
            Error("\n");
            return;
        }

        stage.argv[0] = paths[i];
    }

    CollectBackground();
    Job* job = nullptr;
    for (auto& slot : jobs)
    {
        if (!slot.group)
        {
            job = &slot;
            break;
        }
    }

    if (!job)
    {
        Error("Job table full; wait for a job first.\n");
        return;
    }

    uint64_t previous = HACKOS_ERROR;
    bool success = true;
    for (unsigned i = 0; i < pipeline.count; ++i)
    {
        uint64_t ends[2] = {HACKOS_ERROR, HACKOS_ERROR};
        bool more = i + 1 < pipeline.count;
        if (more && syscall_pipe(ends) != 0)
        {
            success = false;
            break;
        }

        HackosSpawnIo specification = {{previous == HACKOS_ERROR ? 0 : previous, more ? ends[1] : 1, 2}, job->group};
        uint64_t opened[8];
        unsigned count = 0;
        auto& stage = pipeline.stages[i];
        bool ready = Redirect(stage, specification.streams, opened, count);
        uint64_t pid = ready ? syscall_spawn_io(paths[i], stage.argc, stage.argv, &specification) : HACKOS_ERROR;
        for (unsigned j = 0; j < count; ++j)
        {
            syscall_fd_close(opened[j]);
        }

        if (previous != HACKOS_ERROR)
        {
            syscall_fd_close(previous);
        }

        if (more)
        {
            syscall_fd_close(ends[1]);
        }

        previous = ends[0];
        if (pid == HACKOS_ERROR || pid == HACKOS_DENIED)
        {
            success = false;
            break;
        }

        if (!job->group)
        {
            job->group = pid;
            Copy(job->name, paths[0]);
        }
    }

    if (previous != HACKOS_ERROR)
    {
        syscall_fd_close(previous);
    }

    if (!success)
    {
        if (job->group)
        {
            syscall_job_control(job->group, HACKOS_JOB_TERMINATE);
            syscall_job_wait(job->group, nullptr, 0);
            *job = {};
        }

        Error("Pipeline could not start.\n");
        return;
    }

    if (syscall_job_control(job->group, pipeline.background ? HACKOS_JOB_BACKGROUND : HACKOS_JOB_FOREGROUND) != 0)
    {
        syscall_job_control(job->group, HACKOS_JOB_TERMINATE);
        syscall_job_wait(job->group, nullptr, 0);
        *job = {};
        Error("Pipeline could not start.\n");
    }
    else if (pipeline.background)
    {
        JobLine(*job, "Running");
    }
    else
    {
        WaitJob(*job);
    }
}

static void Complete(LineBuffer& line)
{
    CompletionWord word;
    if (line.Overflowed() || !CompletionPrefix(line.Text(), word))
    {
        return;
    }

    size_t prefixLength = 0;
    for (size_t i = 0; word.text[i]; ++i)
    {
        if (word.text[i] == '/')
        {
            prefixLength = i + 1;
        }
    }

    bool command = !word.index && !prefixLength && word.text[0] != '~';
    bool directories = Equal(word.command, "cd");
    char directory[256], rawDirectory[256];
    if (command)
    {
        Copy(directory, "~/USR/BIN");
    }
    else
    {
        for (size_t i = 0; i < prefixLength; ++i)
        {
            rawDirectory[i] = word.text[i];
        }

        rawDirectory[prefixLength] = 0;
        if (!NormalizePath(prefixLength ? rawDirectory : ".", workingDirectory, directory))
        {
            return;
        }
    }

    static const char* builtins[] = {"cd", "history", "logout", "user", "jobs", "fg", "bg", "wait"};
    char common[256] = {};
    unsigned matches = 0;
    bool isDirectory = false;
    // Two passes avoid a large candidates buffer. Nothing is modified if the
    // bounded scan cannot establish the complete candidate set.
    for (unsigned pass = 0; pass < 2; ++pass)
    {
        for (uint64_t i = 0; i <= 1024 + 8; ++i)
        {
            HackosFileInfo info = {};
            if (command && i < 8)
            {
                Copy(info.name, builtins[i]);
            }
            else
            {
                uint64_t index = command ? i - 8 : i;
                if (index == 1024)
                {
                    Print("\nCompletion directory too large.\n");
                    Prompt();
                    Print(line.Text());
                    return;
                }

                if (syscall_readdir(directory, index, &info) != 1)
                {
                    break;
                }
            }

            const char* name = info.name;
            if (!*name || Equal(name, ".") || Equal(name, "..") ||
                (directories && !(info.attributes & HACKOS_DIRECTORY)) ||
                (*name == '.' && word.text[prefixLength] != '.') || !CompletionMatch(name, word.text + prefixLength))
            {
                continue;
            }

            if (pass)
            {
                Print(name);
                Print(info.attributes & HACKOS_DIRECTORY ? "/  " : "  ");
                continue;
            }

            if (!matches++)
            {
                Copy(common, name);
                isDirectory = info.attributes & HACKOS_DIRECTORY;
            }
            else
            {
                size_t length = 0;
                while (common[length] && name[length] && CompletionFold(common[length]) == CompletionFold(name[length]))
                {
                    ++length;
                }

                common[length] = 0;
            }
        }

        if (!matches)
        {
            return;
        }

        if (!pass)
        {
            char replacement[256], completed[256];
            if (prefixLength + Length(common) >= sizeof(completed))
            {
                return;
            }

            for (size_t i = 0; i < prefixLength; ++i)
            {
                completed[i] = word.text[i];
            }

            Copy(completed + prefixLength, common);
            char suffix = matches == 1 ? (isDirectory ? '/' : ' ') : 0;
            if (CompletionReplace(line.Text(), word.start, completed, suffix, replacement))
            {
                for (size_t i = 0; i < line.Length(); ++i)
                {
                    syscall_console(0);
                }

                line.Set(replacement);
                Print(line.Text());
            }

            if (matches == 1)
            {
                return;
            }

            Print("\n");
        }
    }

    Print("\n");
    Prompt();
    Print(line.Text());
}

int main()
{
    Print("\nHackOS ring-3 shell. Type 'help' for commands.\n");
    Prompt();
    LineBuffer line;
    bool discard = false;
    for (;;)
    {
        uint64_t value = syscall_terminal_read(0);
        if (value == HACKOS_DENIED || value == HACKOS_ERROR)
        {
            Error("Shell does not own terminal input.\n");
            return 1;
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

        if (!discard && value == '\t')
        {
            Complete(line);
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
            CollectBackground();
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
