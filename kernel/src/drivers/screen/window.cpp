#include "window.h"
#include "lib/string.h"
#include "lib/memory.h"
#include "screen.h"

extern void putchar_at_xy(Framebuffer fb, char character, uint32_t x, uint32_t y);

static const char* window_title = "Untitled Window";
static const uint32_t navbar_height = 30; 

Window::Window()
    : startX(0), startY(0), sizeX(200), sizeY(200), fullscreen(false),
    resizable(false)
{
    strcpy(title, window_title);

    Framebuffer mainFramebuffer = Display::SharedDisplay()->framebuffer;

    uint32_t size_y = fullscreen ? sizeY : (sizeY - navbar_height);
    uint32_t* drawPointer = new uint32_t[sizeX * size_y];
    memset(drawPointer, 0, sizeX * size_y * sizeof(uint32_t));
    drawArea.address = drawPointer;
    drawArea.bpp = mainFramebuffer.bpp;
    drawArea.width = sizeX;
    drawArea.height = size_y;
    drawArea.pitch = sizeX * (drawArea.bpp / 8);
}

Window::~Window()
{
    delete[] drawArea.address;
}

void Window::Update()
{
}

void Window::Draw(Framebuffer* framebuffer)
{
    Console& mainConsole = Display::SharedDisplay()->console;

    uint32_t old_colour = mainConsole.fontColour;
    mainConsole.fontColour = 0xFFFFFFFF;

    //Draw navbar
    uint32_t colour = 0xFF888888;
    uint32_t borderColour = 0xFF303030;
    uint32_t x = startX + 10;
    uint32_t y = startY + 8;

    framebuffer->DrawRect(startX - 1, startY - 1, sizeX + 2, sizeY + 2, borderColour);
    framebuffer->DrawRect(startX - 1, startY - 1, sizeX + 2, navbar_height + 1, borderColour);
    framebuffer->DrawRect(startX, startY, sizeX, navbar_height - 1, colour);
    framebuffer->DrawRect((startX + sizeX) - 25, startY + 5, 20, 20, 0xFFFF0000);
    putchar_at_xy(*framebuffer, 'X', (startX + sizeX) - 19, startY + 7);
    framebuffer->DrawRect((startX + sizeX) - 41, startY + 7 + 3, 10, 10, 0xFFFFFFFF);
    framebuffer->DrawRect((startX + sizeX) - 41 + 1, startY + 7 + 4, 8, 8, colour);
    putchar_at_xy(*framebuffer, '-', (startX + sizeX) - 63, startY + 7);

    int index = 0;
    char c = 0;
    while((c = title[index]) != 0)
    {
        putchar_at_xy(*framebuffer, c, (x + index * mainConsole.char_width), y);
        index++;
    }

    //Copy window data
    for(size_t y = 0; y < (sizeY - navbar_height); y++)
    {
        for(size_t x = 0; x < sizeX; x++)
        {
            framebuffer->PutPixel(x + startX, y + startY + navbar_height, drawArea.GetPixel(x, y));
        }
    }

    mainConsole.fontColour = old_colour;
}

void Window::MoveWindow(uint32_t x, uint32_t y)
{
    this->startX = x;
    this->startY = y;
}

void Window::ResizeWindow(uint32_t x, uint32_t y)
{
    if(y < (navbar_height + 30))
    {
        y = navbar_height + 30;
    }

    this->sizeX = x;
    this->sizeY = y;

    Framebuffer mainFramebuffer = Display::SharedDisplay()->framebuffer;

    delete[] drawArea.address;
    uint32_t size_y = fullscreen ? sizeY : (sizeY - navbar_height);
    uint32_t* drawPointer = new uint32_t[sizeX * size_y];
    drawArea.address = drawPointer;
    drawArea.bpp = mainFramebuffer.bpp;
    drawArea.width = sizeX;
    drawArea.height = size_y;
    drawArea.pitch = sizeX * (drawArea.bpp / 8);
}

void Window::SetCursorPosiiton(uint32_t x, uint32_t y)
{
    this->cursorX = x;
    this->cursorY = y;
}

void Window::SetTitle(const char* title)
{
    if(strlen((char*)title) < 30)
    {
        strcpy(this->title, title);
    }
}