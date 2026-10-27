#include "command.h"

int main(int argc, char**)
{
    if (!Arguments(argc, 1, "wd"))
    {
        return 2;
    }

    char path[256];
    if (syscall_getcwd(path, sizeof(path)) != 0)
    {
        return 1;
    }

    Print(path);
    Print("\n");
    return 0;
}
