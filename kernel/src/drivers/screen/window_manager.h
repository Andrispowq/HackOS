#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include "window.h"

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
    void Draw(const FramebufferDamage& clip);

    Window* CreateWindow(uint32_t startX, uint32_t startY, uint32_t sizeX, uint32_t sizeY, uint64_t owner = 0);
    Window* GetWindow(uint64_t ID);
    void DestroyWindow(Window* window);
    void DestroyWindowByID(uint64_t ID);
    void Keyboard(const GuiKey& key);

    uint64_t FocusedOwner() const
    {
        return focused ? focused->GetOwner() : 0;
    }

    void SetPanel(Window* window);
    bool SetBackground(Window* window);
    void RequestClose(Window* window);

    bool HasPanel() const
    {
        return panel != nullptr;
    }

    uint64_t Activate(Window* window);

    uint64_t ChangeMode(Window* window, uint64_t operation);

  private:
    void Focus(Window* window);
    void CycleFocus(bool reverse);

    void Mouse(int32_t x, int32_t y, uint32_t buttons);
    Window* Hit(int32_t x, int32_t y);
    void FocusTop();
    unsigned MinimizedCount() const;
    void DrawRestoreBar();
    void Wheel(int32_t x, int32_t y, int32_t delta);

    Window* panel = nullptr;
    Window* background = nullptr;
    Window* focused = nullptr;
    Window* capture = nullptr;
    unsigned captureMode = 0; // 1 client, 2 title drag, 3 resize grip
    int32_t anchorX = 0, anchorY = 0;
    uint32_t originalWidth = 0, originalHeight = 0;
    uint32_t previousButtons = 0, suppressedButtons = 0, keyModifiers = 0;
    uint64_t shortcutKeys[4] = {};

    struct WindowList
    {
        Window* ptr[16] = {};
        unsigned count = 0;

        unsigned size() const
        {
            return count;
        }

        Window*& operator[](unsigned index)
        {
            return ptr[index];
        }

        Window* operator[](unsigned index) const
        {
            return ptr[index];
        }

        void push_back(Window* window)
        {
            ptr[count++] = window;
        }

        void erase(unsigned index)
        {
            for (unsigned i = index + 1; i < count; ++i)
            {
                ptr[i - 1] = ptr[i];
            }
            --count;
        }
    } windows;

    Window slots[16];

    Framebuffer drawBuffer;
};

extern WindowManager* KernelWindowManager;

#endif
