#include "command.h"

int main(int argc, char** argv)
{
    if (!Arguments(argc, 2, "rm <file>"))
    {
        return 2;
    }

    if (syscall_unlink(argv[1]))
    {
        Error("rm: failed (missing, protected, open or non-file path, or I/O error).\n");
        return 1;
    }

    return 0;
}
