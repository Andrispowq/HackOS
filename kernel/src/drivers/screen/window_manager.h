#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include "window.h"
#include "lib/data_structures/vector.h"

class WindowManager
{
public:
    WindowManager();
    ~WindowManager();

    void Update();
    void Draw();

    Window* CreateWindow(uint32_t startX, uint32_t startY, uint32_t sizeX, uint32_t sizeY);
    void DestroyWindow(Window* window);

private:
    vector<Window*> windows;

    Framebuffer drawBuffer;
};

#endif