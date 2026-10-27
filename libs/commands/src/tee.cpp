#include "filter.h"

int main(int argc, char** argv)
{
    const char* usage = "Usage: tee [-a] [--] [file ...]\n";
    bool append = false;
    int first = 1;
    for (; first < argc; ++first)
    {
        const char* option = argv[first];
        if (Equal(option, "--help"))
        {
            return FilterUsage(usage, true);
        }
        if (Equal(option, "--"))
        {
            ++first;
            break;
        }
        if (option[0] != '-' || !option[1])
        {
            break;
        }
        for (unsigned i = 1; option[i]; ++i)
        {
            if (option[i] != 'a')
            {
                return FilterUsage(usage);
            }
            append = true;
        }
    }
    if (argc - first > (int)HACKOS_MAX_ARGS)
    {
        return FilterUsage(usage);
    }

    uint64_t files[HACKOS_MAX_ARGS];
    bool ok = true, standard = true, broken = false;
    for (int i = first; i < argc; ++i)
    {
        files[i - first] = syscall_fd_open(argv[i], HACKOS_OPEN_WRITE | HACKOS_OPEN_CREATE |
                                                        (append ? HACKOS_OPEN_APPEND : HACKOS_OPEN_TRUNCATE));
        if (files[i - first] >= HACKOS_FD_LIMIT)
        {
            FilterError("tee", argv[i], "cannot open");
            ok = false;
        }
    }

    char buffer[HACKOS_IO_LIMIT];
    while (!broken)
    {
        uint64_t length = syscall_fd_read(0, buffer, sizeof(buffer), HACKOS_WAIT_FOREVER);
        if (length > sizeof(buffer))
        {
            FilterError("tee", nullptr, "read failed");
            ok = false;
            break;
        }
        if (!length)
        {
            break;
        }
        if (standard)
        {
            uint64_t offset = 0;
            while (offset < length)
            {
                uint64_t count = syscall_fd_write(1, buffer + offset, length - offset, HACKOS_WAIT_FOREVER);
                if (!count || count > length - offset)
                {
                    broken = count == HACKOS_BROKEN_PIPE;
                    if (!broken)
                    {
                        FilterError("tee", nullptr, "write failed");
                    }
                    ok = standard = false;
                    break;
                }
                offset += count;
            }
        }
        if (broken)
        {
            break;
        }
        bool active = standard;
        for (int i = first; i < argc; ++i)
        {
            auto& fd = files[i - first];
            if (fd < HACKOS_FD_LIMIT)
            {
                if (!Write(fd, buffer, length))
                {
                    FilterError("tee", argv[i], "write failed");
                    ok = false;
                    syscall_fd_close(fd);
                    fd = HACKOS_ERROR;
                }
                else
                {
                    active = true;
                }
            }
        }
        if (!active)
        {
            break;
        }
    }
    for (int i = first; i < argc; ++i)
    {
        if (files[i - first] < HACKOS_FD_LIMIT && syscall_fd_close(files[i - first]))
        {
            FilterError("tee", argv[i], "close/flush failed");
            ok = false;
        }
    }
    return ok ? 0 : 1;
}
