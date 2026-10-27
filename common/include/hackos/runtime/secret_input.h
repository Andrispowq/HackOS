#pragma once
#include "hackos/user.h"

namespace User
{
inline void EraseSecret(void* buffer, uint64_t length)
{
    auto bytes = (volatile uint8_t*)buffer;
    while (length--)
    {
        *bytes++ = 0;
    }
}

// Hidden input is never echoed, including to the debug console. Overflow or
// Ctrl-C discards the entire value rather than accepting a truncated password.
inline bool ReadLine(char* buffer, uint64_t capacity, bool hidden)
{
    uint64_t used = 0;
    bool overflow = false;
    EraseSecret(buffer, capacity);
    for (;;)
    {
        uint64_t ch = syscall_terminal_read(0);
        if (ch == HACKOS_DENIED || ch == HACKOS_ERROR || ch == HACKOS_KEY_INTERRUPT)
        {
            EraseSecret(buffer, capacity);
            Print("\n");
            return false;
        }

        if (ch == HACKOS_INPUT_OVERFLOW)
        {
            overflow = true;
        }
        else if (ch == '\n')
        {
            Print("\n");
            if (overflow)
            {
                EraseSecret(buffer, capacity);
                return false;
            }

            return true;
        }
        else if (!overflow && ch == '\b' && used)
        {
            buffer[--used] = 0;
            if (!hidden)
            {
                syscall_console(0);
            }
        }
        else if (!overflow && ch >= 32 && ch <= 126)
        {
            if (used + 1 == capacity)
            {
                overflow = true;
                continue;
            }

            buffer[used++] = ch;
            if (!hidden)
            {
                syscall_user_write(buffer + used - 1, 1);
            }
        }
    }
}

inline bool NewSecret(char (&password)[HACKOS_PASSWORD_MAX + 1])
{
    char confirmation[HACKOS_PASSWORD_MAX + 1] = {};
    Print("New password: ");
    bool ok = ReadLine(password, sizeof(password), true);
    if (ok)
    {
        Print("Confirm password: ");
        ok = ReadLine(confirmation, sizeof(confirmation), true);
    }

    ok = ok && Length(password) >= 8 && Equal(password, confirmation);
    EraseSecret(confirmation, sizeof(confirmation));
    if (!ok)
    {
        EraseSecret(password, sizeof(password));
        Print("Passwords must match and contain 8-128 printable characters.\n");
    }

    return ok;
}
}
