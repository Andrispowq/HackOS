#include "command.h"

int main(int argc, char** argv)
{
    if (argc > 2)
    {
        Print("Usage: ls [path]\n");
        return 2;
    }

    const char* path = argc == 2 ? argv[1] : ".";
    HackosFileInfo info;
    for (uint64_t i = 0; ; ++i)
    {
        uint64_t result = syscall_readdir(path, i, &info);
        if (result == 0) break;
        if (result == HACKOS_ERROR)
        {
            Print("Cannot list directory.\n");
            return 1;
        }

        Print(info.name);
        Print(info.attributes & HACKOS_DIRECTORY ? "/ " : " ");
    }

    Print("\n");
    return 0;
}
