#pragma once
#include "hackos/abi.h"

// Separate from physical input: one atomic paste, drained before later keystrokes.
struct TerminalPaste
{
    char bytes[HACKOS_CLIPBOARD_LIMIT];
    unsigned position, length;

    void Clear()
    {
        for (auto& byte : bytes)
        {
            byte = 0;
        }
        position = length = 0;
    }

    bool Pending() const
    {
        return position < length;
    }

    bool Set(const char* source, unsigned size)
    {
        if (size > sizeof(bytes) || Pending())
        {
            return false;
        }
        for (unsigned i = 0; i < size; ++i)
        {
            unsigned char ch = source[i];
            if ((ch < 32 || ch > 126) && ch != '\r' && ch != '\n' && ch != '\t')
            {
                return false;
            }
        }
        Clear();
        for (unsigned i = 0; i < size; ++i)
        {
            char ch = source[i];
            if (ch == '\n' && i && source[i - 1] == '\r')
            {
                continue;
            }
            bytes[length++] = ch == '\r' || ch == '\n' || ch == '\t' ? ' ' : ch;
        }
        return true;
    }

    bool Read(char& ch)
    {
        if (!Pending())
        {
            return false;
        }
        ch = bytes[position];
        bytes[position++] = 0;
        return true;
    }
};
