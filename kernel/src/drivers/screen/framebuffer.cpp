#include "framebuffer.h"
#include "lib/memory.h"

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