#include "command.h"
#include "hackos/stdio.h"

int main(int argc, char** argv)
{
    if (argc != 1 && !(argc == 2 && Equal(argv[1], "aux")))
    {
        Error("Usage: ps [aux]\n");
        return 2;
    }

    if (printf("%6s %6s %6s %10s %10s %-8s %s\n", "PID", "PPID", "PGID", "UID", "GID", "STATE", "NAME") < 0)
    {
        return 1;
    }

    HackosProcessInfo info;
    for (uint64_t i = 0; syscall_process_info(i, &info) == 1; ++i)
    {
        const char* state = info.state == 0   ? "ALIVE"
                            : info.state == 3 ? "WAITING"
                            : info.state == 4 ? "INPUT"
                            : info.state == 5 ? "STOPPED"
                            : info.state == 6 ? "IO"
                            : info.state == 7 ? "STARTING"
                            : info.state == 8 ? "SLEEP"
                                              : "ZOMBIE";
        if (printf("%6lu %6lu %6lu %10u %10u %-8s %s\n", info.pid, info.parentPid, info.processGroup, info.uid,
                   info.gid, state, info.name) < 0)
        {
            return 1;
        }
    }

    return 0;
}
