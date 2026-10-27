#pragma once
#include "lib/stdint.h"

// Translated PS/2 set 1, US layout. No allocation or display work in the IRQ.
class KeyboardInput
{
    char queue[256] = {};
    uint16_t read = 0, write = 0;
    bool leftShift = false, rightShift = false, caps = false, capsDown = false;
    bool extended = false, overflow = false, interrupt = false;
    bool leftControl = false, rightControl = false;
    uint8_t pauseBytes = 0;

  public:
    void PushScancode(uint8_t code)
    {
        if (pauseBytes)
        {
            --pauseBytes;
            return;
        }

        if (code == 0xe1)
        {
            pauseBytes = 5;
            extended = false;
            return;
        }

        if (code == 0xe0)
        {
            extended = true;
            return;
        }

        bool released = code & 0x80;
        code &= 0x7f;
        char ch = 0;
        if (extended)
        {
            extended = false;
            if (code == 0x1d)
            {
                rightControl = !released;
                return;
            }

            if (!released && code == 0x48)
            {
                ch = (char)128;
            }

            if (!released && code == 0x50)
            {
                ch = (char)129;
            }

            if (!released && code == 0x1c)
            {
                ch = '\n';
            }

            if (!released && code == 0x35)
            {
                ch = '/';
            }
        }
        else
        {
            if (code == 0x1d)
            {
                leftControl = !released;
                return;
            }

            if (code == 0x2a)
            {
                leftShift = !released;
                return;
            }

            if (code == 0x36)
            {
                rightShift = !released;
                return;
            }

            if (code == 0x3a)
            {
                if (!released && !capsDown)
                {
                    caps = !caps;
                }

                capsDown = !released;
                return;
            }

            if (released)
            {
                return;
            }

            static const char plain[] = "\0\0"
                                        "1234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
            static const char shifted[] = "\0\0"
                                          "!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";
            if (code >= sizeof(plain) - 1)
            {
                return;
            }

            ch = plain[code];
            if ((leftControl || rightControl) && ch == 'c')
            {
                interrupt = true;
                Clear();
                ch = 3;
            }

            bool shift = leftShift || rightShift;
            if (ch == 3)
            {
            }
            else if (ch >= 'a' && ch <= 'z')
            {
                ch = (shift != caps) ? shifted[code] : ch;
            }
            else if (shift)
            {
                ch = shifted[code];
            }
        }

        if (!ch)
        {
            return;
        }

        uint16_t next = (write + 1) & 255;
        if (next == read)
        {
            overflow = true;
            return;
        }

        queue[write] = ch;
        write = next;
    }

    void Clear()
    {
        read = write;
        overflow = false;
    }

    bool TakeInterrupt()
    {
        bool pending = interrupt;
        interrupt = false;
        return pending;
    }

    bool Pop(char& ch)
    {
        if (read == write)
        {
            return false;
        }

        ch = queue[read];
        read = (read + 1) & 255;
        return true;
    }

    bool DiscardOverflow()
    {
        if (!overflow)
        {
            return false;
        }

        overflow = false;
        read = write;
        return true;
    }
};
