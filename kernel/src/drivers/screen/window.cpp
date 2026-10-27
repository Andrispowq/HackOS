#include "window.h"
#include "lib/string.h"
#include "lib/memory.h"
#include "screen.h"

#include "arch/x86_64/interrupts/guard.h"

extern void putchar_at_xy(Framebuffer fb, char character, uint32_t x, uint32_t y);

static const char* window_title = "Untitled Window";
static const uint32_t navbar_height = 30;

static uint64_t next_window_ID = 1;

Window::Window() : startX(0), startY(0), sizeX(200), sizeY(200), fullscreen(false), resizable(false)
{
    ID = next_window_ID++;

    strcpy(title, window_title);

    Framebuffer mainFramebuffer = Display::SharedDisplay()->framebuffer;

    uint32_t size_y = fullscreen ? sizeY : (sizeY - navbar_height);
    uint32_t* drawPointer = new uint32_t[sizeX * size_y];
    memset(drawPointer, 0, sizeX * size_y * sizeof(uint32_t));
    drawArea = new Framebuffer();
    drawArea->address = drawPointer;
    drawArea->bpp = mainFramebuffer.bpp;
    drawArea->width = sizeX;
    drawArea->height = size_y;
    drawArea->pitch = sizeX;
}

Window::~Window()
{
    delete[] drawArea->address;
    delete drawArea;
}

HackosWindowState Window::State() const
{
    return {startX, startY, sizeX, sizeY - navbar_height, mouseX, mouseY, mouseButtons, focused};
}

void Window::SetFocused(bool value)
{
    focused = value;
    if (!value)
    {
        mouseButtons = 0;
    }

    SendEvent(HACKOS_WINDOW_FOCUS);
}

void Window::SetMouse(int32_t x, int32_t y, uint32_t buttons)
{
    mouseX = x;
    mouseY = y;
    mouseButtons = buttons;
}

void Window::SendEvent(uint32_t type, uint32_t key)
{
    InterruptGuard guard;
    HackosWindowEvent event = {type, key, mouseX, mouseY, sizeX, sizeY - navbar_height, mouseButtons, focused};
    if (type == HACKOS_WINDOW_MOVE)
    {
        event.x = startX;
        event.y = startY;
    }

    events.Push(event);
}

bool Window::ReadEvent(HackosWindowEvent& event)
{
    InterruptGuard guard;
    if (events.TakeOverflow())
    {
        event = {HACKOS_WINDOW_OVERFLOW, 0, mouseX, mouseY, sizeX, sizeY - navbar_height, mouseButtons, focused};
        return true;
    }

    return events.Pop(event);
}

void Window::InputOverflow()
{
    InterruptGuard guard;
    events.Overflow();
}

void Window::Draw(Framebuffer* framebuffer)
{
    //Draw navbar
    uint32_t colour = focused ? 0xFF366B96 : 0xFF666666;
    uint32_t borderColour = 0xFF303030;
    uint32_t x = startX + 10;
    uint32_t y = startY + 8;

    // Only paint the border. Filling the whole client first would overwrite
    // millions of pixels that CopyFrom immediately replaces again.
    framebuffer->DrawRect(startX - 1, startY - 1, 1, sizeY + 2, borderColour);
    framebuffer->DrawRect(startX + sizeX, startY - 1, 1, sizeY + 2, borderColour);
    framebuffer->DrawRect(startX, startY + sizeY, sizeX, 1, borderColour);
    framebuffer->DrawRect(startX - 1, startY - 1, sizeX + 2, navbar_height + 1, borderColour);
    framebuffer->DrawRect(startX, startY, sizeX, navbar_height - 1, colour);
    framebuffer->DrawRect((startX + sizeX) - 25, startY + 5, 20, 20, 0xFFFF0000);
    putchar_at_xy(*framebuffer, 'X', (startX + sizeX) - 19, startY + 7);
    framebuffer->DrawRect(startX + sizeX - 49, startY + 5, 20, 20, borderColour);
    putchar_at_xy(*framebuffer, maximised ? 'R' : '+', startX + sizeX - 43, startY + 7);
    framebuffer->DrawRect(startX + sizeX - 73, startY + 5, 20, 20, borderColour);
    putchar_at_xy(*framebuffer, '-', startX + sizeX - 67, startY + 7);

    int index = 0;
    char c = 0;
    while ((c = title[index]) != 0 && 10 + (index + 1) * 8 < sizeX - 78)
    {
        putchar_at_xy(*framebuffer, c, (x + index * 8), y);
        index++;
    }

    framebuffer->CopyFrom(*drawArea, startX, startY + navbar_height);

    if (!maximised)
    {
        framebuffer->DrawRect(startX + sizeX - 8, startY + sizeY - 8, 8, 8, borderColour);
    }
}

void Window::MoveWindow(uint32_t x, uint32_t y)
{
    this->startX = x;
    this->startY = y;
}

void Window::ResizeWindow(uint32_t x, uint32_t y)
{
    if (y < (navbar_height + 30))
    {
        y = navbar_height + 30;
    }

    this->sizeX = x;
    this->sizeY = y;

    Framebuffer mainFramebuffer = Display::SharedDisplay()->framebuffer;

    delete[] drawArea->address;
    uint32_t size_y = fullscreen ? sizeY : (sizeY - navbar_height);
    uint32_t* drawPointer = new uint32_t[sizeX * size_y];
    memset(drawPointer, 0, sizeX * size_y * sizeof(uint32_t));
    drawArea->address = drawPointer;
    drawArea->bpp = mainFramebuffer.bpp;
    drawArea->width = sizeX;
    drawArea->height = size_y;
    drawArea->pitch = sizeX;
}

void Window::SetCursorPosiiton(uint32_t x, uint32_t y)
{
    this->cursorX = x;
    this->cursorY = y;
}

void Window::SetTitle(const char* title)
{
    if (strlen((char*)title) < 30)
    {
        strcpy(this->title, title);
    }
}

uint32_t Window::Mode() const
{
    return (minimised ? HACKOS_WINDOW_MINIMIZED : 0) | (maximised ? HACKOS_WINDOW_MAXIMIZED : 0);
}

void Window::SaveNormalGeometry()
{
    normalX = startX;
    normalY = startY;
    normalWidth = sizeX;
    normalHeight = sizeY;
}

void Window::RestoreNormalGeometry()
{
    MoveWindow(normalX, normalY);
    ResizeWindow(normalWidth, normalHeight);
}
