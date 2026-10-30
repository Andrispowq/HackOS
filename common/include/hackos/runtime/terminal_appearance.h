#pragma once
#include "hackos/abi.h"

struct TerminalAppearance
{
    uint32_t height, background, foreground;

    void Reset()
    {
        height = 16;
        background = 0x101b25ff;
        foreground = 0xe5eef5ff;
    }

    bool Configure(uint64_t key, uint64_t value)
    {
        if (key == HACKOS_TERMINAL_DEFAULT && value >= 1 && value <= 3)
        {
            key = value;
            value = key == 1 ? 16 : key == 2 ? 0x101b25ff : 0xe5eef5ff;
        }
        if (key == HACKOS_TERMINAL_RESET && !value)
        {
            Reset();
        }
        else if (key == HACKOS_TERMINAL_FONT_SIZE && value >= 8 && value <= 64 && !(value % 2))
        {
            height = value;
        }
        else if (key == HACKOS_TERMINAL_BG_COLOUR && value <= 0xffffffff)
        {
            background = value;
        }
        else if (key == HACKOS_TERMINAL_FONT_COLOUR && value <= 0xffffffff)
        {
            foreground = value;
        }
        else
        {
            return false;
        }
        return true;
    }

    HackosTerminalSettings Settings(unsigned columns, unsigned rows) const
    {
        return {height, background, foreground, columns, rows, 0};
    }
};

inline uint32_t TerminalBlend(uint32_t rgba, uint32_t background)
{
    unsigned alpha = rgba & 255;
    uint32_t result = 0xff000000;
    for (unsigned shift = 0; shift < 24; shift += 8)
    {
        result |= ((((rgba >> (shift + 8)) & 255) * alpha + ((background >> shift) & 255) * (255 - alpha) + 127) / 255)
                  << shift;
    }
    return result;
}

inline bool TerminalGlyphPixel(const uint8_t* glyphs, uint8_t character, unsigned x, unsigned y, unsigned height)
{
    return (glyphs[character * 16 + y * 16 / height] & (0x80 >> (x * 8 / (height / 2)))) != 0;
}

inline uint32_t TerminalAnsiColour(unsigned index, uint32_t fallback, bool bold = false)
{
    static constexpr uint32_t palette[16] = {0xff000000, 0xffaa0000, 0xff00aa00, 0xffaa5500, 0xff0000aa, 0xffaa00aa,
                                             0xff00aaaa, 0xffaaaaaa, 0xff555555, 0xffff5555, 0xff55ff55, 0xffffff55,
                                             0xff5555ff, 0xffff55ff, 0xff55ffff, 0xffffffff};
    if (!index || index > 16)
    {
        return fallback;
    }
    if (bold && index <= 8)
    {
        index += 8;
    }
    return palette[index - 1];
}
