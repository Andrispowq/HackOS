#include "command.h"

int main(int argc, char** argv)
{
    if (argc > 2)
    {
        Error("Usage: cat [path] (or read [path])\n");
        return 2;
    }

    uint64_t fd = argc == 2 ? syscall_fd_open(argv[1], HACKOS_OPEN_READ) : HACKOS_STDIN;
    if (fd >= HACKOS_FD_LIMIT)
    {
        Error("File not found.\n");
        return 1;
    }

    char buffer[HACKOS_IO_LIMIT];
    int status = 0;
    for (;;)
    {
        uint64_t length = syscall_fd_read(fd, buffer, sizeof(buffer), HACKOS_WAIT_FOREVER);
        if (!length)
        {
            break;
        }

        if (length > sizeof(buffer) || !Write(HACKOS_STDOUT, buffer, length))
        {
            Error("Read/write failed.\n");
            status = 1;
            break;
        }
    }

    if (argc == 2 && syscall_fd_close(fd) != 0)
    {
        Error("Close failed.\n");
        status = 1;
    }

    return status;
}
