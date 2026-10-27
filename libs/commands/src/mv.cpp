#include "command.h"

int main(int argc, char** argv)
{
    if (!Arguments(argc, 3, "mv <source> <destination>"))
    {
        return 2;
    }

    if (syscall_rename(argv[1], argv[2]))
    {
        Print("mv: failed; destination must be a new file path on the same filesystem.\n");
        Print("Check for open/read-only files, invalid paths or I/O errors.\n");
        return 1;
    }

    return 0;
}
