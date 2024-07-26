#include "framebuffer.h"
#include "memory.h"

void Framebuffer::PutPixel(uint32_t x, uint32_t y, uint32_t colour)
{
    if((x >= width) || (y >= height))
    {
        return;
    }

    address[y * width + x] = colour;
}

uint32_t Framebuffer::GetPixel(uint32_t x, uint32_t y)
{
    if((x >= width) || (y >= height))
    {
        return 0x0;
    }

    return address[y * width + x];
}

void Framebuffer::DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t colour)
{
    uint32_t x_end = x + width;
    uint32_t y_end = y + height;

    if((x >= this->width) || (y >= this->height))
    {
        return;
    }    

    if((x_end >= this->width) || (y_end >= this->height))
    {
        x_end = this->width;
        y_end = this->height;
    }

    for(uint32_t _y = y; _y < y_end; _y++)
    {
        for(uint32_t _x = x; _x < x_end; _x++)
        {
            address[_y * this->width + _x] = colour;
        }
    }
}

void Framebuffer::Clear(uint32_t colour)
{
    DrawRect(0, 0, width, height, colour);
}