#pragma once
#include "hackos/types.h"

// Session-only history: 256 complete 255-character command lines. No heap use.
class CommandHistory
{
    char commands[256][256] = {};
    char draft[256] = {};
    uint64_t next = 0, count = 0, position = 0;

    static void Copy(char* out, const char* in)
    {
        unsigned i = 0;
        while (i < 255 && in[i])
        {
            out[i] = in[i];
            ++i;
        }

        out[i] = 0;
    }

  public:
    uint64_t Count() const
    {
        return count;
    }

    const char* At(uint64_t index) const
    {
        return index < count ? commands[(next + 256 - count + index) % 256] : "";
    }

    void ResetNavigation()
    {
        position = count;
        draft[0] = 0;
    }

    void Add(const char* text)
    {
        const char* p = text;
        while (*p == ' ' || *p == '\t')
        {
            ++p;
        }

        if (*p)
        {
            Copy(commands[next], text);
            next = (next + 1) % 256;
            if (count < 256)
            {
                ++count;
            }
        }

        ResetNavigation();
    }

    const char* Up(const char* current)
    {
        if (!count)
        {
            return current;
        }

        if (position == count)
        {
            Copy(draft, current);
        }

        if (position)
        {
            --position;
        }

        return At(position);
    }

    const char* Down(const char* current)
    {
        if (position == count)
        {
            return current;
        }

        ++position;
        return position == count ? draft : At(position);
    }
};
