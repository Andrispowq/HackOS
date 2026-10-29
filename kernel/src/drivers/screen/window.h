#ifndef WINDOW_H
#define WINDOW_H

#include "lib/stdint.h"

#include "framebuffer.h"
#include "hackos/abi.h"
#include "drivers/keyboard/gui_input.h"
#include "hackos/runtime/event_queue.h"

class Window
{
  public:
    Window();
    ~Window();

    void SetDesktopPanel()
    {
        desktopPanel = true;
    }

    void SetDesktopBackground()
    {
        desktopBackground = true;
    }

    bool IsDesktopBackground() const
    {
        return desktopBackground;
    }

    void SetOwner(uint64_t pid)
    {
        owner = pid;
    }

    uint64_t GetOwner() const
    {
        return owner;
    }

    void SetFocused(bool value, uint32_t modifiers = 0);
    void SetMouse(int32_t x, int32_t y, uint32_t buttons);
    HackosWindowState State() const;
    void SendEvent(uint32_t type, uint32_t key = 0);
    bool ReadEvent(HackosWindowEvent& event);
    bool ReadEvent2(HackosWindowEvent2& event);

    bool RichInput() const
    {
        return richInput;
    }

    void EnableInput();
    void ResetInput(uint32_t modifiers);
    void Key(const GuiKey& key);
    void RequestClose();

    bool ClosePending() const
    {
        return closePending;
    }

    void RejectClose();
    HackosWindowInputState InputState() const;
    void InputOverflow();
    uint32_t Mode() const;
    void SaveNormalGeometry();
    bool RestoreNormalGeometry();
    void Release();
    bool Initialise(uint32_t width, uint32_t height);
    uint64_t Frame(uint64_t operation, HackosWindowFrame& frame);
    Framebuffer* UploadBuffer(uint64_t generation);

    bool EventsReady() const
    {
        return events.Ready() || (closePending && !closeDelivered);
    }

    bool IsFramed() const
    {
        return framed;
    }

    void Draw(Framebuffer* framebuffer);

    //Getters
    uint64_t GetWindowID() const
    {
        return ID;
    }

    bool ShouldBeClosed() const
    {
        return toBeClosed;
    }

    uint32_t GetStartX() const
    {
        return startX;
    }

    uint32_t GetStartY() const
    {
        return startY;
    }

    uint32_t GetSizeX() const
    {
        return sizeX;
    }

    uint32_t GetSizeY() const
    {
        return sizeY;
    }

    bool GetFullscreen() const
    {
        return fullscreen;
    }

    bool GetResizable() const
    {
        return resizable;
    }

    bool GetBorderless() const
    {
        return borderless;
    }

    bool GetMaximised() const
    {
        return maximised;
    }

    bool GetMinimised() const
    {
        return minimised;
    }

    bool GetVisible() const
    {
        return visible;
    }

    bool GetCursorVisible() const
    {
        return cursor_visible;
    }

    bool GetCursorGrabbed() const
    {
        return cursor_grabbed;
    }

    uint32_t GetCursorX() const
    {
        return cursorX;
    }

    uint32_t GetCursorY() const
    {
        return cursorY;
    }

    const char* GetTitle() const
    {
        return title;
    }

    Framebuffer* GetDrawBuffer()
    {
        return drawArea;
    }

    //Setters
    void MoveWindow(uint32_t x, uint32_t y);
    bool ResizeWindow(uint32_t x, uint32_t y);

    void SetCursorPosiiton(uint32_t x, uint32_t y);
    void SetTitle(const char* title);

    void SetFullscreen(bool fullscreen)
    {
        this->fullscreen = fullscreen;
    }

    void SetResizable(bool resizable)
    {
        this->resizable = resizable;
    }

    void SetBorderless(bool borderless)
    {
        this->borderless = borderless;
    }

    void SetMaximised(bool maximised)
    {
        this->maximised = maximised;
    }

    void SetMinimised(bool minimised)
    {
        this->minimised = minimised;
    }

    void SetVisible(bool visible)
    {
        this->visible = visible;
    }

    void SetCursorVisible(bool cursor_visible)
    {
        this->cursor_visible = cursor_visible;
    }

    void SetCursorGrabbed(bool cursor_grabbed)
    {
        this->cursor_grabbed = cursor_grabbed;
    }

  private:
    void Invalidate();
    uint32_t normalX = 0, normalY = 0, normalWidth = 0, normalHeight = 0;
    uint64_t owner = 0;
    bool focused = false;
    int32_t mouseX = 0, mouseY = 0;
    uint32_t mouseButtons = 0;
    EventQueue<HackosWindowEvent2, 64> events;
    bool richInput = false, closePending = false, closeDelivered = false;
    uint64_t pressedKeys[4] = {}, inputSerial = 0;
    uint32_t modifiers = 0;
    HackosWindowEvent2 MakeEvent(uint32_t type, uint32_t key = 0, uint32_t flags = 0) const;

    bool desktopPanel = false, desktopBackground = false;
    bool toBeClosed = false;

    uint64_t ID = 0;

    uint32_t startX = 0, startY = 0;
    uint32_t sizeX = 600, sizeY = 400;
    bool fullscreen = false;
    bool resizable = true;
    bool borderless = false;
    bool maximised = false;
    bool minimised = false;
    bool visible = true;
    bool cursor_visible = false;
    bool cursor_grabbed = false;
    uint32_t cursorX = 0, cursorY = 0;
    char title[30];

    Framebuffer front = {}, staging = {};
    Framebuffer* drawArea = &front;
    uint64_t generation = 1;
    bool transaction = false;
    bool framed = false;
};

#endif
