#pragma once
#include "filter.h"

inline bool Blank(unsigned char ch)
{
    return ch == ' ' || ch == '\t';
}

inline unsigned char Fold(unsigned char ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + 'a' - 'A' : ch;
}

struct FilterLine
{
    char data[64 * 1024];
    unsigned length;

    // 1 for a line (including a final partial line), 0 for EOF, -1 for error.
    int Read(FilterInput& input, const char* command, const char* path)
    {
        length = 0;
        int ch;
        while ((ch = input.Next()) >= 0 && ch != '\n')
        {
            if (length == sizeof(data))
            {
                FilterError(command, path, "line exceeds 64 KiB");
                return -1;
            }
            data[length++] = ch;
        }
        return ch == -2 ? -1 : ch == '\n' || length ? 1 : 0;
    }
};
