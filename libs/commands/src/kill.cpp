#include "command.h"

int main(int argc, char** argv)
{
    if (!Arguments(argc, 2, "kill <pid>"))
    {
        return 2;
    }

    uint64_t pid = 0;
    if (!*argv[1])
    {
        Error("Invalid PID.\n");
        return 2;
    }

    for (const char* digit = argv[1]; *digit; ++digit)
    {
        if (*digit < '0' || *digit > '9' || pid > (~0ULL - (*digit - '0')) / 10)
        {
            Error("Invalid PID.\n");
            return 2;
        }

        pid = pid * 10 + *digit - '0';
    }

    uint64_t result = syscall_kill(pid);
    if (result == 0)
    {
        Print("Task stopped.\n");
    }
    else
    {
        Error(result == HACKOS_DENIED ? "Cannot stop the idle or shell task, or permission denied.\n"
                                      : "No running task with that PID.\n");
    }

    return result == 0 ? 0 : 1;
}
