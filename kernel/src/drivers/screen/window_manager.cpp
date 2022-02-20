#include "window_manager.h"
#include "drivers/screen/screen.h"

WindowManager::WindowManager()
{
    drawBuffer = Display::SharedDisplay()->backbuffer;
}

WindowManager::~WindowManager()
{
    for(size_t i = 0; i < windows.size(); i++)
    {
        DestroyWindow(windows[i]);
    }
}

void WindowManager::Update()
{
    for(size_t i = 0; i < windows.size(); i++)
    {
        windows[i]->Update();
    }
}

void WindowManager::Draw()
{
    for(size_t i = 0; i < windows.size(); i++)
    {
        windows[i]->Draw(&drawBuffer);
    }

    Display::SharedDisplay()->DrawBackbuffer();
}

Window* WindowManager::CreateWindow(uint32_t startX, uint32_t startY, uint32_t sizeX, uint32_t sizeY)
{
    Window* window = new Window();
    window->MoveWindow(startX, startY);
    window->ResizeWindow(sizeX, sizeY);
    window->Update();

    windows.push_back(window);
    return window;
}

void WindowManager::DestroyWindow(Window* window)
{
    for(size_t i = 0; i < windows.size(); i++)
    {
        if(windows[i] == window)
        {
            windows.erase(i);
            delete window;
            break;
        }
    }
}