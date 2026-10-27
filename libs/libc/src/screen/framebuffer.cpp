#include "framebuffer.h"
#include "memory.h"

void Framebuffer::PutPixel(uint32_t x, uint32_t y, uint32_t colour)
{
    if ((x >= width) || (y >= height))
    {
        return;
    }

    address[(uint64_t)y * pitch + x] = colour;
}

uint32_t Framebuffer::GetPixel(uint32_t x, uint32_t y)
{
    if ((x >= width) || (y >= height))
    {
        return 0x0;
    }

    return address[(uint64_t)y * pitch + x];
}

void Framebuffer::DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t colour)
{
    if (x >= this->width || y >= this->height)
    {
        return;
    }

    uint32_t x_end = x + (width < this->width - x ? width : this->width - x);
    uint32_t y_end = y + (height < this->height - y ? height : this->height - y);
    for (uint32_t row = y; row < y_end; ++row)
    {
        for (uint32_t column = x; column < x_end; ++column)
        {
            address[(uint64_t)row * pitch + column] = colour;
        }
    }
}

void Framebuffer::Clear(uint32_t colour)
{
    DrawRect(0, 0, width, height, colour);
}
