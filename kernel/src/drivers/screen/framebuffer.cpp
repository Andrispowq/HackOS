#include "framebuffer.h"
#include "lib/memory.h"

void Framebuffer::PutPixel(uint32_t x, uint32_t y, uint32_t colour)
{
    if((x >= width) || (y >= height))
    {
        return;
    }

    address[(uint64_t)y * pitch + x] = colour;
}

uint32_t Framebuffer::GetPixel(uint32_t x, uint32_t y)
{
    if((x >= width) || (y >= height))
    {
        return 0x0;
    }

    return address[(uint64_t)y * pitch + x];
}

void Framebuffer::DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t colour)
{
    if (x >= this->width || y >= this->height) return;
    uint32_t x_end = x + (width < this->width - x ? width : this->width - x);
    uint32_t y_end = y + (height < this->height - y ? height : this->height - y);
    for (uint32_t row = y; row < y_end; ++row)
        for (uint32_t column = x; column < x_end; ++column)
            address[(uint64_t)row * pitch + column] = colour;
}

void Framebuffer::Clear(uint32_t colour)
{
    DrawRect(0, 0, width, height, colour);
}

bool MouseDrawn = false;
uint32_t MouseCursorBuffer[16 * 16];
uint32_t MouseCursorBufferAfter[16 * 16];
void Framebuffer::DrawMouseCursor(uint8_t* cursor, uint32_t x, uint32_t y, uint32_t colour)
{
    int xMax = 16;
    int yMax = 16;
    int differenceX = width - x;
    int differenceY = height - y;

    if (differenceX < 16) xMax = differenceX;
    if (differenceY < 16) yMax = differenceY;

    for (int _y = 0; _y < yMax; _y++)
    {
        for (int _x = 0; _x < xMax; _x++)
        {
            int bit = _y * 16 + _x;
            int byte = bit / 8;
            if ((cursor[byte] & (0b10000000 >> (_x % 8))))
            {
                MouseCursorBuffer[_x + _y * 16] = GetPixel(x + _x, y + _y);
                PutPixel(x + _x, y + _y, colour);
                MouseCursorBufferAfter[_x + _y * 16] = GetPixel(x + _x, y + _y);

            }
        }
    }

    MouseDrawn = true;
}

void Framebuffer::ClearMouseCursor(uint8_t* cursor, uint32_t x, uint32_t y)
{
    if (!MouseDrawn) 
    {
        return;
    }

    int xMax = 16;
    int yMax = 16;
    int differenceX = width - x;
    int differenceY = height - y;

    if (differenceX < 16) xMax = differenceX;
    if (differenceY < 16) yMax = differenceY;

    for (int _y = 0; _y < yMax; _y++)
    {
        for (int _x = 0; _x < xMax; _x++)
        {
            int bit = _y * 16 + _x;
            int byte = bit / 8;
            if ((cursor[byte] & (0b10000000 >> (_x % 8))))
            {
                if (GetPixel(x + _x, y + _y) == MouseCursorBufferAfter[_x + _y *16])
                {
                    PutPixel(x + _x, y + _y, MouseCursorBuffer[_x + _y * 16]);
                }
            }
        }
    }
}