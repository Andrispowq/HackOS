#include "console.h"
#include "lib/memory.h"

static uint32_t Blend(uint32_t front, uint32_t back)
{
    uint32_t alpha = front >> 24;
    uint32_t result = 0xff000000;
    for (unsigned shift = 0; shift < 24; shift += 8)
    {
        uint32_t channel = (((front >> shift) & 255) * alpha + ((back >> shift) & 255) * (255 - alpha) + 127) / 255;
        result |= channel << shift;
    }
    return result;
}

uint32_t Console::Background() const
{
    // The console is the bottom layer; its background blends over black.
    return Blend(bgColour, 0xff000000);
}

uint32_t Console::Foreground() const
{
    return Blend(fontColour, Background());
}

HackosTerminalSettings Console::Settings() const
{
    return {char_height,
            (bgColour << 8) | (bgColour >> 24),
            (fontColour << 8) | (fontColour >> 24),
            con_width,
            con_height,
            0};
}

bool Console::Configure(uint64_t key, uint64_t value, uint32_t width, uint32_t height)
{
    Console next = *this;
    if (key == HACKOS_TERMINAL_DEFAULT && value >= 1 && value <= 3)
    {
        key = value;
        value = key == 1 ? 30 : key == 2 ? 0x000000ff : 0xffffffff;
    }
    if (key == HACKOS_TERMINAL_RESET && value == 0)
    {
        next = Console{};
    }
    else if (key == HACKOS_TERMINAL_FONT_SIZE)
    {
        if (value < 8 || value > 64 || value % 2)
        {
            return false;
        }
        next.char_height = value;
        next.char_width = value / 2;
    }
    else if ((key == HACKOS_TERMINAL_BG_COLOUR || key == HACKOS_TERMINAL_FONT_COLOUR) && value <= 0xffffffff)
    {
        uint32_t argb = ((uint32_t)value >> 8) | ((uint32_t)value << 24);
        if (key == HACKOS_TERMINAL_BG_COLOUR)
        {
            next.bgColour = argb;
        }
        else
        {
            next.fontColour = argb;
        }
    }
    else
    {
        return false;
    }

    next.con_width = width / next.char_width;
    next.con_height = height / next.char_height;
    if (!next.con_width || !next.con_height)
    {
        return false;
    }
    next.current_x = 0;
    next.current_y = 0;
    *this = next;
    return true;
}

void DrawConsoleGlyph(Framebuffer& framebuffer, const PSF1_FONT& font, uint8_t character, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height, uint32_t colour)
{
    if (!font.psf1_Header || !font.glyphBuffer || !font.psf1_Header->charsize || !width || !height ||
        x >= framebuffer.width || y >= framebuffer.height)
    {
        return;
    }

    uint32_t nativeHeight = font.psf1_Header->charsize;
    const uint8_t* glyph = (const uint8_t*)font.glyphBuffer + character * nativeHeight;
    uint32_t left = x, top = y, columns = width, rows = height;
    if (!framebuffer.ClipRect(left, top, columns, rows))
    {
        return;
    }

    for (uint32_t row = top; row < top + rows; ++row)
    {
        uint8_t bits = glyph[(uint64_t)(row - y) * nativeHeight / height];
        for (uint32_t column = left; column < left + columns; ++column)
        {
            if (bits & (0x80 >> ((uint64_t)(column - x) * 8 / width)))
            {
                framebuffer.address[(uint64_t)row * framebuffer.pitch + column] = colour;
            }
        }
    }
}

void Console::Scroll(Framebuffer& framebuffer) const
{
    uint32_t rows = framebuffer.height / char_height;
    if (rows < 2)
    {
        framebuffer.Clear(Background());
        return;
    }

    uint32_t keptHeight = (rows - 1) * char_height;
    Framebuffer remaining = framebuffer;
    remaining.address += (uint64_t)char_height * framebuffer.pitch;
    remaining.height = keptHeight;
    // Top-to-bottom rows move toward lower addresses; each row is disjoint
    // from its source. Reuse the bounded pixel-copy path instead of byte memcpy.
    framebuffer.CopyFrom(remaining, 0, 0);
    framebuffer.DrawRect(0, keptHeight, framebuffer.width, framebuffer.height - keptHeight, Background());
}
