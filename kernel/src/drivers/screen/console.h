#pragma once
#include "drivers/screen/framebuffer.h"
#include "lib/font/font.h"
#include "hackos/abi.h"

struct Console
{
    uint32_t bgColour = 0xff505050, fontColour = 0xff00ffff;
    uint32_t current_x = 0, current_y = 0;
    uint32_t con_width = 0, con_height = 0;
    uint32_t char_width = 8, char_height = 16;

    bool Configure(uint64_t key, uint64_t value, uint32_t width, uint32_t height);
    HackosTerminalSettings Settings() const;
    uint32_t Background() const;
    uint32_t Foreground() const;
    void Scroll(Framebuffer& framebuffer) const;
};

void DrawConsoleGlyph(Framebuffer& framebuffer, const PSF1_FONT& font, uint8_t character, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height, uint32_t colour);
