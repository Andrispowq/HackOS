#pragma once
#include "filter.h"

// A deliberately bounded BRE subset: literals, '.', '^', '$', '*', bracket
// sets/ranges and escaped metacharacters. Unsupported BRE operators are errors.
// State-set simulation has O(line bytes * pattern atoms) work, no recursion or
// exponential backtracking, even for patterns such as a*a*a*a*b.
class GrepPattern
{
  public:
    bool Compile(const char* text, bool fixed, bool ignoreCase)
    {
        insensitive = ignoreCase;
        for (const char* ch = text; *ch; ++ch)
        {
            if (*ch == '\n')
            {
                return false;
            }
        }
        if (!fixed && *text == '^')
        {
            start = true;
            ++text;
        }
        while (*text)
        {
            if (!fixed && *text == '$' && !text[1])
            {
                end = true;
                break;
            }
            if (count == Limit)
            {
                return false;
            }
            auto& atom = atoms[count++];
            unsigned char ch = *text++;
            if (!fixed && ch == '[')
            {
                bool negate = *text == '^';
                text += negate;
                bool first = true;
                while (*text && (first || *text != ']'))
                {
                    unsigned char low = *text++, high = low;
                    first = false;
                    // POSIX classes/collation are not silently treated as text.
                    if (low == '[' && (*text == ':' || *text == '.' || *text == '='))
                    {
                        return false;
                    }
                    if (*text == '-' && text[1] && text[1] != ']')
                    {
                        high = text[1];
                        text += 2;
                        if (high < low)
                        {
                            return false;
                        }
                    }
                    for (unsigned value = low; value <= high; ++value)
                    {
                        Set(atom, value);
                    }
                }
                if (*text++ != ']')
                {
                    return false;
                }
                atom.negate = negate;
            }
            else if (!fixed && ch == '.')
            {
                for (auto& byte : atom.bits)
                {
                    byte = 255;
                }
            }
            else
            {
                if (!fixed && ch == '\\')
                {
                    ch = *text++;
                    if (!ch ||
                        !(ch == '\\' || ch == '.' || ch == '^' || ch == '$' || ch == '*' || ch == '[' || ch == ']'))
                    {
                        return false;
                    }
                }
                Set(atom, ch);
            }
            if (!fixed && *text == '*')
            {
                atom.repeat = true;
                ++text;
                if (*text == '*')
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool Match(const char* line, unsigned size) const
    {
        bool active[Limit + 1] = {};
        active[0] = true;
        for (unsigned position = 0;; ++position)
        {
            if (!start)
            {
                active[0] = true;
            }
            for (unsigned i = 0; i < count; ++i)
            {
                if (active[i] && atoms[i].repeat)
                {
                    active[i + 1] = true;
                }
            }
            if (active[count] && (!end || position == size))
            {
                return true;
            }
            if (position == size)
            {
                return false;
            }
            bool next[Limit + 1] = {};
            unsigned char ch = line[position];
            for (unsigned i = 0; i < count; ++i)
            {
                const auto& atom = atoms[i];
                bool matches = (atom.bits[ch / 8] & (1U << (ch % 8))) != 0;
                if (active[i] && matches != atom.negate)
                {
                    next[i + (atom.repeat ? 0 : 1)] = true;
                }
            }
            for (unsigned i = 0; i <= count; ++i)
            {
                active[i] = next[i];
            }
        }
    }

  private:
    static constexpr unsigned Limit = 128;

    struct Atom
    {
        unsigned char bits[32] = {};
        bool repeat = false, negate = false;
    };

    Atom atoms[Limit];
    unsigned count = 0;
    bool start = false, end = false, insensitive = false;

    void Set(Atom& atom, unsigned ch)
    {
        atom.bits[ch / 8] |= 1U << (ch % 8);
        if (insensitive && ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')))
        {
            ch ^= 32;
            atom.bits[ch / 8] |= 1U << (ch % 8);
        }
    }
};
