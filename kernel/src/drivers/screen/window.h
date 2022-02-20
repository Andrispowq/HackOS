#ifndef WINDOW_H
#define WINDOW_H

#include "lib/stdint.h"

#include "framebuffer.h"

class Window
{
public:
    Window();
    ~Window();

    void Update();
    void Draw(Framebuffer* framebuffer);

    //Getters
    uint32_t GetStartX() const { return startX; }
    uint32_t GetStartY() const { return startY; }

    uint32_t GetSizeX() const { return sizeX; }
    uint32_t GetSizeY() const { return sizeY; }

    bool GetFullscreen() const { return fullscreen; }
    bool GetResizable() const { return resizable; }
    bool GetBorderless() const { return borderless; }
    bool GetMaximised() const { return maximised; }
    bool GetMinimised() const { return minimised; }
    bool GetVisible() const { return visible; }
    bool GetCursorVisible() const { return cursor_visible; }
    bool GetCursorGrabbed() const { return cursor_grabbed; }

    uint32_t GetCursorX() const { return cursorX; }
    uint32_t GetCursorY() const { return cursorY; }

    const char* GetTitle() const { return title; }

    Framebuffer GetDrawBuffer() { return drawArea; }

    //Setters
    void MoveWindow(uint32_t x, uint32_t y);
    void ResizeWindow(uint32_t x, uint32_t y);

    void SetCursorPosiiton(uint32_t x, uint32_t y);
    void SetTitle(const char* title);

    void SetFullscreen(bool fullscreen) { this->fullscreen = fullscreen; }
    void SetResizable(bool resizable) { this->resizable = resizable; }
    void SetBorderless(bool borderless) { this->borderless = borderless; }
    void SetMaximised(bool maximised) { this->maximised = maximised; }
    void SetMinimised(bool minimised) { this->minimised = minimised; }
    void SetVisible(bool visible) { this->visible = visible; }
    void SetCursorVisible(bool cursor_visible) { this->cursor_visible = cursor_visible; }
    void SetCursorGrabbed(bool cursor_grabbed) { this->cursor_grabbed = cursor_grabbed; }

private:
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

    Framebuffer drawArea;
};

#endif