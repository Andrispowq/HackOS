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
        Print("Invalid PID.\n");
        return 2;
    }

    for (const char* digit = argv[1]; *digit; ++digit)
    {
        if (*digit < '0' || *digit > '9' || pid > (~0ULL - (*digit - '0')) / 10)
        {
            Print("Invalid PID.\n");
            return 2;
        }

        pid = pid * 10 + *digit - '0';
    }

    uint64_t result = syscall_kill(pid);
    Print(result == HACKOS_DENIED  ? "Cannot stop the idle or shell task, or permission denied.\n"
          : result == HACKOS_ERROR ? "No running task with that PID.\n"
                                   : "Task stopped.\n");
    return result == 0 ? 0 : 1;
}
