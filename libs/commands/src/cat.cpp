#include "command.h"

int main(int argc, char** argv)
{
    if (!Arguments(argc, 2, "cat <path> (or read <path>)")) return 2;

    uint64_t file = syscall_open(argv[1]);
    if (file == HACKOS_ERROR)
    {
        Print("File not found.\n");
        return 1;
    }

    char buffer[512];
    int status = 0;
    for (;;)
    {
        uint64_t length = syscall_read(file, buffer, sizeof(buffer));
        if (!length) break;
        if (length == HACKOS_ERROR || syscall_user_write(buffer, length) != length)
        {
            Print("Read failed.\n");
            status = 1;
            break;
        }
    }

    syscall_close(file);
    Print("\n");
    return status;
}
