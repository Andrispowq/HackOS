#include "command.h"

int main(int argc, char** argv)
{
    if (!Arguments(argc, 3, "chmod <octal-mode> <path>"))
    {
        return 2;
    }

    uint64_t mode = 0;
    unsigned digits = 0;
    for (const char* c = argv[1]; *c; ++c)
    {
        if (*c < '0' || *c > '7' || ++digits > 3)
        {
            Error("Mode must be three octal digits or fewer.\n");
            return 2;
        }

        mode = mode * 8 + *c - '0';
    }

    if (!digits || syscall_chmod(argv[2], mode))
    {
        Error("Cannot change permissions.\n");
        return 1;
    }

    return 0;
}
