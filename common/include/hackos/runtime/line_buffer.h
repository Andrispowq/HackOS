#pragma once
#include "hackos/types.h"

class LineBuffer
{
    char text[256] = {};
    size_t length = 0;
    bool tooLong = false;

  public:
    enum Result
    {
        Ignored,
        Echo,
        Erase,
        Submit,
        Overflow
    };

    Result Push(char ch)
    {
        if (ch == '\n')
        {
            return tooLong ? Overflow : Submit;
        }

        if (tooLong)
        {
            return Ignored; // discard the entire overlong command
        }

        if (ch == '\b')
        {
            if (!length)
            {
                return Ignored;
            }

            text[--length] = 0;
            return Erase;
        }

        if (ch < ' ' || ch > '~')
        {
            return Ignored;
        }

        if (length == sizeof(text) - 1)
        {
            tooLong = true;
            return Ignored;
        }

        text[length++] = ch;
        text[length] = 0;
        return Echo;
    }

    bool Overflowed() const
    {
        return tooLong;
    }

    size_t Length() const
    {
        return length;
    }

    void Set(const char* value)
    {
        Reset();
        while (value[length] && length < sizeof(text) - 1)
        {
            text[length] = value[length];
            ++length;
        }

        text[length] = 0;
    }

    char* Text()
    {
        return text;
    }

    void Reset()
    {
        length = 0;
        text[0] = 0;
        tooLong = false;
    }
};
