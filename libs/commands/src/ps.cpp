#include "command.h"

int main(int argc, char** argv)
{
    if (argc != 1 && !(argc == 2 && Equal(argv[1], "aux")))
    {
        Error("Usage: ps [aux]\n");
        return 2;
    }

    HackosProcessInfo info;
    for (uint64_t i = 0; syscall_process_info(i, &info) == 1; ++i)
    {
        Print("Process: ");
        Print(info.name);
        Print(" (");
        Number(info.pid);
        Print(info.state == 0   ? ") ALIVE"
              : info.state == 3 ? ") WAITING"
              : info.state == 4 ? ") INPUT"
              : info.state == 5 ? ") STOPPED"
              : info.state == 6 ? ") IO"
              : info.state == 7 ? ") STARTING"
              : info.state == 8 ? ") SLEEP"
                                : ") ZOMBIE");
        Print(" uid=");
        Number(info.uid);
        Print(" gid=");
        Number(info.gid);
        Print(" ppid=");
        Number(info.parentPid);
        Print(" pgid=");
        Number(info.processGroup);
        Print("\n");
    }

    return 0;
}
