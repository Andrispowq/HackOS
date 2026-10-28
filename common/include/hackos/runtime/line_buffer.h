#pragma once
#include "hackos/types.h"

class LineBuffer
{
    char text[256] = {};
    size_t length = 0;
    size_t cursor = 0;
    bool tooLong = false;
    bool editing;

  public:
    explicit LineBuffer(bool middleEditing = false) : editing(middleEditing)
    {
    }

    enum Result
    {
        Ignored,
        Echo,
        Erase,
        Submit,
        Overflow,
        Redraw,
        Moved
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

        uint8_t key = (uint8_t)ch;
        if (editing && (key == 130 || key == 131))
        {
            if (key == 130 && cursor)
            {
                --cursor;
            }
            if (key == 131 && cursor < length)
            {
                ++cursor;
            }
            return Moved;
        }

        if (editing && key == 132)
        {
            if (cursor == length)
            {
                return Ignored;
            }
            for (size_t i = cursor; i < length; ++i)
            {
                text[i] = text[i + 1];
            }
            --length;
            return Redraw;
        }

        if (ch == '\b')
        {
            if (!cursor)
            {
                return Ignored;
            }

            bool atEnd = cursor == length;
            --cursor;
            for (size_t i = cursor; i < length; ++i)
            {
                text[i] = text[i + 1];
            }
            --length;
            return atEnd ? Erase : Redraw;
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

        bool atEnd = cursor == length;
        for (size_t i = length + 1; i > cursor; --i)
        {
            text[i] = text[i - 1];
        }
        text[cursor++] = ch;
        ++length;
        return atEnd ? Echo : Redraw;
    }

    size_t Cursor() const
    {
        return cursor;
    }

    void SetCursor(size_t position)
    {
        cursor = position < length ? position : length;
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
        cursor = length;
    }

    const char* Text() const
    {
        return text;
    }

    char* Text()
    {
        return text;
    }

    void Reset()
    {
        length = cursor = 0;
        text[0] = 0;
        tooLong = false;
    }
};
