#include "window.h"
#include "lib/string.h"
#include "lib/memory.h"
#include "screen.h"
#include "arch/x86_64/paging/page_frame_allocator.h"
#include "proc/tasking/process.h"

#include "arch/x86_64/interrupts/guard.h"

extern void putchar_at_xy(Framebuffer fb, char character, uint32_t x, uint32_t y);

static const char* window_title = "Untitled Window";
static const uint32_t navbar_height = 30;

static uint64_t next_window_ID = 1;

uint64_t window_surface_pages;

static bool AllocateSurface(Framebuffer& surface, uint32_t width, uint32_t height)
{
    uint64_t pages = ((uint64_t)width * height * 4 + 4095) / 4096;
    if (!pages || pages > 16384 - window_surface_pages)
    {
        return false;
    }

    auto address = (uint32_t*)PageFrameAllocator::SharedAllocator()->TryRequestSurfacePages(pages);
    if (!address)
    {
        return false;
    }

    window_surface_pages += pages;
    memset(address, 0, pages * 4096);
    surface = {address, width, height, 32, width};
    return true;
}

static void FreeSurface(Framebuffer& surface)
{
    if (surface.address)
    {
        uint64_t pages = ((uint64_t)surface.width * surface.height * 4 + 4095) / 4096;
        PageFrameAllocator::SharedAllocator()->FreePages(surface.address, pages);
        window_surface_pages -= pages;
        surface = {};
    }
}

Window::Window()
{
    strcpy(title, window_title);
}

bool Window::Initialise(uint32_t width, uint32_t height)
{
    if (!ResizeWindow(width, height))
    {
        return false;
    }

    ID = next_window_ID++;
    return true;
}

Window::~Window()
{
    Release();
}

void Window::Release()
{
    FreeSurface(front);
    FreeSurface(staging);
    ID = 0;
    events.Clear();
    richInput = closePending = closeDelivered = false;
    modifiers = 0;
    for (auto& keys : pressedKeys)
    {
        keys = 0;
    }
    transaction = framed = focused = maximised = minimised = false;
    mouseButtons = 0;
    mouseX = mouseY = 0;
    owner = 0;
    visible = true;
}

uint64_t Window::Frame(uint64_t operation, HackosWindowFrame& frame)
{
    if (operation == HACKOS_WINDOW_BEGIN)
    {
        if (!staging.address && !AllocateSurface(staging, sizeX, sizeY - navbar_height))
        {
            return HACKOS_ERROR;
        }

        memset(staging.address, 0, (uint64_t)staging.width * staging.height * 4);
        // Begin starts a complete replacement frame, initially black.
        transaction = framed = true;
        ++generation;
        frame = {generation, sizeX, sizeY - navbar_height};
        return 0;
    }

    if (operation > HACKOS_WINDOW_CANCEL)
    {
        return HACKOS_ERROR;
    }

    if (!transaction || frame.generation != generation)
    {
        return HACKOS_WINDOW_STALE;
    }

    if (operation == HACKOS_WINDOW_PRESENT)
    {
        FreeSurface(front);
        front = staging;
        staging = {};
        Invalidate();
    }
    else
    {
        FreeSurface(staging);
    }

    transaction = false;
    return 0;
}

Framebuffer* Window::UploadBuffer(uint64_t token)
{
    return transaction && token == generation ? &staging : nullptr;
}

HackosWindowState Window::State() const
{
    return {startX, startY, sizeX, sizeY - navbar_height, mouseX, mouseY, mouseButtons, focused};
}

void Window::SetFocused(bool value, uint32_t currentModifiers)
{
    focused = value;
    modifiers = value ? currentModifiers : 0;
    if (richInput)
    {
        ResetInput(value ? currentModifiers : 0);
    }
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

HackosWindowEvent2 Window::MakeEvent(uint32_t type, uint32_t key, uint32_t flags) const
{
    HackosWindowEvent event = {type, key, mouseX, mouseY, sizeX, sizeY - navbar_height, mouseButtons, focused};
    if (type == HACKOS_WINDOW_MOVE)
    {
        event.x = startX;
        event.y = startY;
    }
    return {event, modifiers, flags, inputSerial};
}

void Window::SendEvent(uint32_t type, uint32_t key)
{
    InterruptGuard guard;
    events.Push(MakeEvent(type, key));
    WakeStreamWaiters();
}

bool Window::ReadEvent(HackosWindowEvent& event)
{
    HackosWindowEvent2 extended;
    if (!ReadEvent2(extended))
    {
        return false;
    }
    event = extended.event;
    return true;
}

bool Window::ReadEvent2(HackosWindowEvent2& event)
{
    InterruptGuard guard;
    if (events.TakeOverflow())
    {
        closeDelivered = false;
        event = MakeEvent(HACKOS_WINDOW_OVERFLOW);
        return true;
    }
    while (events.Pop(event))
    {
        if (event.event.type == HACKOS_WINDOW_CLOSE_REQUEST)
        {
            if (!closePending)
            {
                continue;
            }
            closeDelivered = true;
        }
        return true;
    }
    if (closePending && !closeDelivered)
    {
        closeDelivered = true;
        event = MakeEvent(HACKOS_WINDOW_CLOSE_REQUEST);
        return true;
    }
    return false;
}

void Window::ResetInput(uint32_t currentModifiers)
{
    events.Clear();
    for (auto& keys : pressedKeys)
    {
        keys = 0;
    }
    modifiers = currentModifiers;
    mouseButtons = 0;
    ++inputSerial;
    closeDelivered = false;
    SendEvent(HACKOS_WINDOW_INPUT_RESET);
}

void Window::EnableInput()
{
    richInput = true;
    ResetInput(modifiers);
}

HackosWindowInputState Window::InputState() const
{
    return {{pressedKeys[0], pressedKeys[1], pressedKeys[2], pressedKeys[3]},
            inputSerial,
            modifiers,
            mouseButtons,
            focused,
            closePending};
}

void Window::Key(const GuiKey& key)
{
    modifiers = key.modifiers;
    uint64_t bit = 1ULL << (key.key % 64);
    auto& word = pressedKeys[key.key / 64];
    // A key held across a focus/reset boundary cannot start repeating actions
    // in its new target. Its release still updates authoritative state.
    if ((key.flags & HACKOS_KEY_REPEAT) && !(word & bit))
    {
        return;
    }
    if (key.down)
    {
        word |= bit;
    }
    else
    {
        word &= ~bit;
    }
    events.Push(MakeEvent(key.down ? HACKOS_WINDOW_KEY_DOWN : HACKOS_WINDOW_KEY_UP, key.key, key.flags));
    if (key.text)
    {
        events.Push(MakeEvent(HACKOS_WINDOW_TEXT, key.text, key.flags));
    }
    WakeStreamWaiters();
}

void Window::RequestClose()
{
    if (!closePending)
    {
        closePending = true;
        closeDelivered = false;
        // Preserve preceding text edits; the latch is the fallback after overflow.
        events.Push(MakeEvent(HACKOS_WINDOW_CLOSE_REQUEST));
        WakeStreamWaiters();
    }
}

void Window::RejectClose()
{
    closePending = closeDelivered = false;
    unsigned count = events.Count();
    HackosWindowEvent2 event;
    for (unsigned i = 0; i < count; ++i)
    {
        events.Pop(event);
        if (event.event.type != HACKOS_WINDOW_CLOSE_REQUEST)
        {
            events.Push(event);
        }
    }
}

void Window::InputOverflow()
{
    InterruptGuard guard;
    events.Overflow();
    WakeStreamWaiters();
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

    if (front.width < sizeX)
    {
        framebuffer->DrawRect(startX + front.width, startY + navbar_height, sizeX - front.width, sizeY - navbar_height,
                              0xff000000);
    }
    if (front.height < sizeY - navbar_height)
    {
        framebuffer->DrawRect(startX, startY + navbar_height + front.height, sizeX,
                              sizeY - navbar_height - front.height, 0xff000000);
    }
    Framebuffer clipped = front;
    if (clipped.width > sizeX)
    {
        clipped.width = sizeX;
    }
    if (clipped.height > sizeY - navbar_height)
    {
        clipped.height = sizeY - navbar_height;
    }
    framebuffer->CopyFrom(clipped, startX, startY + navbar_height);

    if (!maximised)
    {
        framebuffer->DrawRect(startX + sizeX - 8, startY + sizeY - 8, 8, 8, borderColour);
    }
}

void Window::Invalidate()
{
    Display::SharedDisplay()->InvalidateRect(startX ? startX - 1 : 0, startY ? startY - 1 : 0, sizeX + 2, sizeY + 2);
}

void Window::MoveWindow(uint32_t x, uint32_t y)
{
    Invalidate();
    this->startX = x;
    this->startY = y;
    Invalidate();
}

bool Window::ResizeWindow(uint32_t x, uint32_t y)
{
    if (x < 96 || x > 4096 || y < 60 || y > 2190)
    {
        return false;
    }

    Framebuffer replacement = {};
    if (!AllocateSurface(replacement, x, y - navbar_height))
    {
        return false;
    }

    Invalidate();
    // Framed clients retain their last committed pixels until presenting.
    if (framed)
    {
        FreeSurface(staging);
        staging = replacement;
        transaction = false;
    }
    else
    {
        FreeSurface(front);
        front = replacement;
    }

    sizeX = x;
    sizeY = y;
    ++generation;
    Invalidate();
    return true;
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

bool Window::RestoreNormalGeometry()
{
    if (!ResizeWindow(normalWidth, normalHeight))
    {
        return false;
    }

    MoveWindow(normalX, normalY);
    return true;
}
