#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

class Framebuffer
{
public:
    void PutPixel(uint32_t x, uint32_t y, uint32_t colour);
    uint32_t GetPixel(uint32_t x, uint32_t y);
    void DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t colour);

    void Clear(uint32_t colour);

    uint32_t* address;
    uint32_t width, height, bpp, pitch;
};

#endif