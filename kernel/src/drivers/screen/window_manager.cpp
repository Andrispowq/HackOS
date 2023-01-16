#include "window_manager.h"
#include "drivers/screen/screen.h"

WindowManager* KernelWindowManager;

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
    size_t size = windows.size();
    for(size_t i = 0; i < size; i++)
    {
        windows[i]->Update();

        if(windows[i]->ShouldBeClosed())
        {
            delete windows[i];
            windows.erase(i);
            size--;
        }
    }
}

void WindowManager::Draw()
{
    for(size_t i = 0; i < windows.size(); i++)
    {
        windows[i]->Draw(&drawBuffer);
    }
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

void WindowManager::DestroyWindowByID(uint64_t ID)
{
    for(size_t i = 0; i < windows.size(); i++)
    {
        if(windows[i]->GetWindowID() == ID)
        {
            Window* window = windows[i];
            windows.erase(i);
            delete window;
            break;
        }
    }
}