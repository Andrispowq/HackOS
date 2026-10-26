#pragma once
#include "lib/stdint.h"

class LineBuffer
{
    char text[256] = {};
    size_t length = 0;
    bool tooLong = false;
public:
    enum Result { Ignored, Echo, Erase, Submit, Overflow };
    Result Push(char ch)
    {
        if (ch == '\n') return tooLong ? Overflow : Submit;
        if (tooLong) return Ignored; // discard the entire overlong command
        if (ch == '\b')
        {
            if (!length) return Ignored;
            text[--length] = 0;
            return Erase;
        }
        if (ch < ' ' || ch > '~') return Ignored;
        if (length == sizeof(text) - 1) { tooLong = true; return Ignored; }
        text[length++] = ch;
        text[length] = 0;
        return Echo;
    }
    char* Text() { return text; }
    void Reset() { length = 0; text[0] = 0; tooLong = false; }
};
