#include "framebuffer.h"

void Framebuffer::PutPixel(uint32_t x, uint32_t y, uint32_t colour)
{
    address[y * width + x] = colour;
}

uint32_t Framebuffer::GetPixel(uint32_t x, uint32_t y)
{
    return address[y * width + x];
}

void Framebuffer::DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t colour)
{
    uint32_t x_end = x + width;
    uint32_t y_end = y + height;

    for(uint32_t _y = 0; _y < y_end; _y++)
    {
        for(uint32_t _x = 0; _x < x_end; _x++)
        {
            PutPixel(_x, _y, colour);
            //address[y * width + x] = colour;
        }
    }
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