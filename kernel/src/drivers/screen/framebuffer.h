#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "lib/stdint.h"

// A clipped union of scene damage; empty after each presentation.
struct FramebufferDamage
{
    uint32_t left = 0, top = 0, right = 0, bottom = 0;
    void Include(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t screenWidth, uint32_t screenHeight);

    bool Empty() const
    {
        return left == right || top == bottom;
    }
};

class Framebuffer
{
  public:
    void PutPixel(uint32_t x, uint32_t y, uint32_t colour);
    uint32_t GetPixel(uint32_t x, uint32_t y);
    void DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t colour);

    void Clear(uint32_t colour);
    void CopyFrom(const Framebuffer& source, uint32_t x, uint32_t y);

    // Same-coordinate rectangle copy; clips against both buffers.
    void CopyRegion(const Framebuffer& source, uint32_t x, uint32_t y, uint32_t columns, uint32_t rows);
    // Compare RAM buffers only, then write changed spans to this framebuffer.
    uint64_t PresentChanged(const Framebuffer& source, Framebuffer& previous, bool force);

    void DrawMouseCursor(uint8_t* cursor, uint32_t x, uint32_t y, uint32_t colour);
    void ClearMouseCursor(uint8_t* cursor, uint32_t x, uint32_t y);

    uint32_t* address;
    uint32_t width, height, bpp, pitch;
};

#endif
