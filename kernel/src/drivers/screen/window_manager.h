#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include "window.h"
#include "lib/data_structures/vector.h"

#define WINDOW_FLAG_FULLSCREEN 0x1
#define WINDOW_FLAG_BORDERLESS 0x2
#define WINDOW_FLAG_SPAWN_AT_CENTER 0x4
#define WINDOW_FLAG_UNFOCUSED_ON_SPAWN 0x8

class WindowManager
{
public:
    WindowManager();
    ~WindowManager();

    void Update();
    void Draw();

    Window* CreateWindow(uint32_t startX, uint32_t startY, uint32_t sizeX, uint32_t sizeY);
    void DestroyWindow(Window* window);
    void DestroyWindowByID(uint64_t ID);

private:
    vector<Window*> windows;

    Framebuffer drawBuffer;
};

#endif