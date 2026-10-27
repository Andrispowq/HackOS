#include "hackos/user.h"

namespace User
{
bool Write(uint64_t fd, const void* data, uint64_t length)
{
    auto bytes = (const char*)data;
    while (length)
    {
        uint64_t count = length > HACKOS_IO_LIMIT ? HACKOS_IO_LIMIT : length;
        uint64_t result = syscall_fd_write(fd, bytes, count, HACKOS_WAIT_FOREVER);
        if (!result || result > count)
        {
            return false;
        }

        bytes += result;
        length -= result;
    }

    return true;
}

void Print(const char* text)
{
    Write(HACKOS_STDOUT, text, Length(text));
}

void Error(const char* text)
{
    Write(HACKOS_STDERR, text, Length(text));
}

void Number(uint64_t value, unsigned base)
{
    if (base != 10 && base != 16)
    {
        return;
    }

    char buffer[65];
    unsigned n = 64;
    buffer[n] = 0;
    do
    {
        buffer[--n] = "0123456789ABCDEF"[value % base];
        value /= base;
    } while (value);
    Print(buffer + n);
}
}
