#include "hackos/user.h"
namespace User
{
void Print(const char* text)
{
    uint64_t left = Length(text);
    while (left)
    {
        uint64_t length = left > 1024 ? 1024 : left;
        if (syscall_user_write(text, length) != length) return;
        text += length; left -= length;
    }
}
void Number(uint64_t value, unsigned base)
{
    if (base != 10 && base != 16) return;
    char buffer[65]; unsigned n = 64; buffer[n] = 0;
    do { buffer[--n] = "0123456789ABCDEF"[value % base]; value /= base; } while (value);
    Print(buffer + n);
}
}
