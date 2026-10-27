#pragma once
#include "hackos/abi.h"

class TextBuffer
{
  public:
    char text[HACKOS_FILE_LIMIT] = {};
    uint64_t length = 0;
    uint64_t cursor = 0;
    bool dirty = false;

    static bool Printable(uint64_t key)
    {
        return (key >= 32 && key <= 126) || key == '\n' || key == '\t';
    }

    bool Valid() const
    {
        if (length > sizeof(text))
        {
            return false;
        }

        for (uint64_t i = 0; i < length; ++i)
        {
            if (!Printable((uint8_t)text[i]))
            {
                return false;
            }
        }

        return true;
    }

    uint64_t LineStart(uint64_t position) const
    {
        while (position && text[position - 1] != '\n')
        {
            --position;
        }

        return position;
    }

    uint64_t LineEnd(uint64_t position) const
    {
        while (position < length && text[position] != '\n')
        {
            ++position;
        }

        return position;
    }

    bool Key(uint64_t key)
    {
        if (key == HACKOS_KEY_LEFT)
        {
            if (cursor)
            {
                --cursor;
            }
        }
        else if (key == HACKOS_KEY_RIGHT)
        {
            if (cursor < length)
            {
                ++cursor;
            }
        }
        else if (key == HACKOS_KEY_UP || key == HACKOS_KEY_DOWN)
        {
            uint64_t start = LineStart(cursor), column = cursor - start;
            if (key == HACKOS_KEY_UP)
            {
                if (!start)
                {
                    return true;
                }
                start = LineStart(start - 1);
            }
            else
            {
                start = LineEnd(cursor);
                if (start == length)
                {
                    return true;
                }
                ++start;
            }

            uint64_t end = LineEnd(start);
            cursor = column < end - start ? start + column : end;
        }
        else if (key == '\b' || key == HACKOS_KEY_DELETE)
        {
            if (key == '\b')
            {
                if (!cursor)
                {
                    return true;
                }
                --cursor;
            }

            if (cursor < length)
            {
                for (uint64_t i = cursor; i + 1 < length; ++i)
                {
                    text[i] = text[i + 1];
                }

                --length;
                dirty = true;
            }
        }
        else if (Printable(key))
        {
            if (length == sizeof(text))
            {
                return false;
            }
            for (uint64_t i = length; i > cursor; --i)
            {
                text[i] = text[i - 1];
            }

            text[cursor++] = key;
            ++length;
            dirty = true;
        }

        return true;
    }
};
