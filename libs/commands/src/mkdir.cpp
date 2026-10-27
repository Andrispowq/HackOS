#include "command.h"

int main(int argc, char** argv)
{
    if (!Arguments(argc, 2, "mkdir <path>"))
    {
        return 2;
    }

    if (syscall_mkdir(argv[1]))
    {
        Error("mkdir: failed (invalid path, existing entry, read-only directory or I/O error).\n");
        return 1;
    }

    return 0;
}
