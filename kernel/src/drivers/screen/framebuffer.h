#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "lib/stdint.h"

class Framebuffer
{
public:
    void PutPixel(uint32_t x, uint32_t y, uint32_t colour);
    uint32_t GetPixel(uint32_t x, uint32_t y);
    void DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t colour);

    void DrawMouseCursor(uint8_t* cursor, uint32_t x, uint32_t y, uint32_t colour);
    void ClearMouseCursor(uint8_t* cursor, uint32_t x, uint32_t y);

    void DrawBackbuffer();

    uint32_t* address;
    uint32_t width, height, bpp, pitch;
};

#endif