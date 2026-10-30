#pragma once
#include "hackos/runtime/terminal_history.h"

// Allocation-free screen and bounded 7-bit ANSI parser. Existing shell newline,
// destructive backspace, four-column tabs and immediate wrapping remain intact.
struct TerminalScreen
{
    uint32_t columns, rows, x, y;
    char cells[HACKOS_TTY_CELLS];
    uint16_t styles[HACKOS_TTY_CELLS];
    TerminalHistory history;
    uint16_t style, savedStyle, parameters[16];
    unsigned savedX, savedY, mode, count, bytes, cursorMode;
    bool invalid, privateMode;

    bool Caret(bool requested) const
    {
        return cursorMode ? cursorMode == 2 : requested;
    }

    void ResetParser()
    {
        mode = count = bytes = 0;
        invalid = privateMode = false;
    }

    void Erase(unsigned first, unsigned end)
    {
        for (unsigned i = first; i < end; ++i)
        {
            cells[i] = ' ';
            styles[i] = style;
        }
    }

    void Clear()
    {
        ResetParser();
        style = savedStyle = 0;
        savedX = savedY = x = y = 0;
        cursorMode = 0;
        history.Clear();
        for (unsigned i = 0; i < HACKOS_TTY_CELLS; ++i)
        {
            cells[i] = ' ';
            styles[i] = 0;
        }
    }

    void Scroll()
    {
        if (y < rows)
        {
            return;
        }
        history.Push(cells, columns, styles);
        for (unsigned i = 0; i < (rows - 1) * columns; ++i)
        {
            cells[i] = cells[i + columns];
            styles[i] = styles[i + columns];
        }
        Erase((rows - 1) * columns, rows * columns);
        y = rows - 1;
    }

    void Put(uint8_t ch)
    {
        if (ch == '\n')
        {
            x = 0;
            ++y;
        }
        else if (ch == '\r')
        {
            x = 0;
        }
        else if (ch == '\b')
        {
            if (x)
            {
                --x;
            }
            else if (y)
            {
                --y;
                x = columns - 1;
            }
            Erase(y * columns + x, y * columns + x + 1);
        }
        else if (ch == '\t')
        {
            do
            {
                Put(' ');
            } while (x % 4);
        }
        else if (ch >= 32 && ch != 127)
        {
            cells[y * columns + x] = ch;
            styles[y * columns + x] = style;
            if (++x == columns)
            {
                x = 0;
                ++y;
            }
        }
        Scroll();
    }

    unsigned Param(unsigned index, unsigned fallback = 1) const
    {
        return index < count && parameters[index] ? parameters[index] : fallback;
    }

    void Save()
    {
        savedX = x;
        savedY = y;
        savedStyle = style;
    }

    void Restore()
    {
        x = savedX < columns ? savedX : columns - 1;
        y = savedY < rows ? savedY : rows - 1;
        style = savedStyle;
    }

    void Sgr()
    {
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned p = parameters[i];
            if (p == 0)
            {
                style = 0;
            }
            else if (p == 1)
            {
                style |= HACKOS_TTY_BOLD;
            }
            else if (p == 4)
            {
                style |= HACKOS_TTY_UNDERLINE;
            }
            else if (p == 7)
            {
                style |= HACKOS_TTY_REVERSE;
            }
            else if (p == 22)
            {
                style &= ~HACKOS_TTY_BOLD;
            }
            else if (p == 24)
            {
                style &= ~HACKOS_TTY_UNDERLINE;
            }
            else if (p == 27)
            {
                style &= ~HACKOS_TTY_REVERSE;
            }
            else if (p == 39)
            {
                style &= ~31;
            }
            else if (p == 49)
            {
                style &= ~(31 << 5);
            }
            else if (p >= 30 && p <= 37)
            {
                style = (style & ~31) | (p - 29);
            }
            else if (p >= 90 && p <= 97)
            {
                style = (style & ~31) | (p - 81);
            }
            else if (p >= 40 && p <= 47)
            {
                style = (style & ~(31 << 5)) | ((p - 39) << 5);
            }
            else if (p >= 100 && p <= 107)
            {
                style = (style & ~(31 << 5)) | ((p - 91) << 5);
            }
            else if (p == 38 || p == 48)
            {
                // Consume unsupported extended-colour operands as a unit.
                if (i + 1 >= count)
                {
                    break;
                }
                unsigned form = parameters[++i];
                unsigned skip = form == 5 ? 1 : form == 2 ? 3 : count;
                if (skip >= count - i)
                {
                    break;
                }
                i += skip;
            }
        }
    }

    void Dispatch(uint8_t final)
    {
        if (invalid)
        {
            return;
        }
        if (privateMode)
        {
            if (count == 1 && parameters[0] == 25 && (final == 'h' || final == 'l'))
            {
                cursorMode = final == 'l' ? 1 : 2;
            }
            return;
        }
        unsigned n = Param(0), position = y * columns + x;
        switch (final)
        {
        case 'A':
            y = n > y ? 0 : y - n;
            break;
        case 'B':
            y = n >= rows - y ? rows - 1 : y + n;
            break;
        case 'C':
            x = n >= columns - x ? columns - 1 : x + n;
            break;
        case 'D':
            x = n > x ? 0 : x - n;
            break;
        case 'E':
            y = n >= rows - y ? rows - 1 : y + n;
            x = 0;
            break;
        case 'F':
            y = n > y ? 0 : y - n;
            x = 0;
            break;
        case 'G':
            x = n > columns ? columns - 1 : n - 1;
            break;
        case 'd':
            y = n > rows ? rows - 1 : n - 1;
            break;
        case 'H':
        case 'f':
            y = n > rows ? rows - 1 : n - 1;
            n = Param(1);
            x = n > columns ? columns - 1 : n - 1;
            break;
        case 'J':
            n = Param(0, 0);
            if (n == 0)
            {
                Erase(position, columns * rows);
            }
            else if (n == 1)
            {
                Erase(0, position + 1);
            }
            else if (n == 2)
            {
                Erase(0, columns * rows);
            }
            else if (n == 3)
            {
                history.Clear();
            }
            break;
        case 'K':
            n = Param(0, 0);
            if (n == 0)
            {
                Erase(position, (y + 1) * columns);
            }
            else if (n == 1)
            {
                Erase(y * columns, position + 1);
            }
            else if (n == 2)
            {
                Erase(y * columns, (y + 1) * columns);
            }
            break;
        case 'm':
            Sgr();
            break;
        case 's':
            Save();
            break;
        case 'u':
            Restore();
            break;
        default:
            break;
        }
    }

    void Feed(uint8_t ch)
    {
        if (ch == 0x18 || ch == 0x1a)
        {
            ResetParser();
            return;
        }
        // Ignored OSC/DCS/SOS/PM/APC payloads use no buffer, even without ST.
        if (mode == 3 || mode == 4)
        {
            if (ch == 7 || (mode == 4 && ch == '\\'))
            {
                ResetParser();
            }
            else
            {
                mode = ch == 27 ? 4 : 3;
            }
            return;
        }
        if (ch == 27)
        {
            ResetParser();
            mode = 1;
            return;
        }
        if (!mode)
        {
            Put(ch);
            return;
        }
        if (mode == 5)
        {
            if (ch < 0x20 || ch > 0x2f)
            {
                ResetParser();
            }
            return;
        }
        if (mode == 1)
        {
            ResetParser();
            if (ch == '[')
            {
                mode = 2;
                count = 1;
                for (auto& p : parameters)
                {
                    p = 0;
                }
            }
            else if (ch == ']' || ch == 'P' || ch == 'X' || ch == '^' || ch == '_')
            {
                mode = 3;
            }
            else if (ch >= 0x20 && ch <= 0x2f)
            {
                mode = 5;
            }
            else if (ch == '7')
            {
                Save();
            }
            else if (ch == '8')
            {
                Restore();
            }
            else if (ch == 'c')
            {
                Clear();
            }
            return;
        }
        if (bytes < 128)
        {
            ++bytes;
        }
        else
        {
            invalid = true;
        }
        if (ch >= 0x40 && ch <= 0x7e)
        {
            Dispatch(ch);
            ResetParser();
        }
        else if (ch == '?' && bytes == 1)
        {
            privateMode = true;
        }
        else if (ch >= '0' && ch <= '9')
        {
            unsigned value = parameters[count - 1] * 10 + ch - '0';
            if (value > 9999)
            {
                invalid = true;
            }
            else
            {
                parameters[count - 1] = value;
            }
        }
        else if (ch == ';')
        {
            if (count == 16)
            {
                invalid = true;
            }
            else
            {
                parameters[count++] = 0;
            }
        }
        else if (ch >= 0x20)
        {
            invalid = true;
        }
    }
};
