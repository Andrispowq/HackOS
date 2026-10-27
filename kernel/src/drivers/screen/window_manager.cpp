#include "window_manager.h"

#include "drivers/screen/screen.h"
#include "drivers/mouse/mouse.h"
#include "drivers/keyboard/keyboard.h"
#include "arch/x86_64/interrupts/guard.h"
#include "proc/tasking/process.h"

WindowManager* KernelWindowManager;

static const uint32_t restoreBarHeight = 28;
extern void putchar_at_xy(Framebuffer fb, char character, uint32_t x, uint32_t y);

static int32_t Clamp(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;

    return value;
}

WindowManager::WindowManager()
{
    drawBuffer = Display::SharedDisplay()->framebuffer;
}

WindowManager::~WindowManager()
{
    while (windows.size())
    {
        DestroyWindow(windows[0]);
    }
}

void WindowManager::Focus(Window* window)
{
    InterruptGuard guard;
    if (focused == window) return;

    if (focused) focused->SetFocused(false);

    focused = window;
    ClearKeyboard(false);

    if (focused)
    {
        for (size_t i = 0; i < windows.size(); i++)
        {
            if (windows[i] == focused)
            {
                windows.erase(i);
                windows.push_back(focused);
                break;
            }
        }

        focused->SetFocused(true);
    }

    Display::SharedDisplay()->dirty = true;
}

void WindowManager::Keyboard(uint32_t key)
{
    // Idle dispatches keyboard and mouse ingress in order. In particular, a
    // queued click changes focus before a following key is delivered.
    if (!focused) return;

    if (key == HACKOS_KEY_INTERRUPT)
    {
        RequestUserInterrupt(focused->GetOwner());
    }
    else
    {
        focused->SendEvent(HACKOS_WINDOW_KEY, key);
    }
}

Window* WindowManager::Hit(int32_t x, int32_t y)
{
    for (size_t i = windows.size(); i > 0; i--)
    {
        auto window = windows[i - 1];
        if (window->GetMinimised()) continue;

        if (x >= (int32_t)window->GetStartX() && y >= (int32_t)window->GetStartY() &&
            x < (int32_t)(window->GetStartX() + window->GetSizeX()) &&
            y < (int32_t)(window->GetStartY() + window->GetSizeY()))
        {
            return window;
        }
    }

    return nullptr;
}

void WindowManager::Mouse(int32_t x, int32_t y, uint32_t buttons)
{
    bool pressed = (buttons & 1) && !(previousButtons & 1);
    previousButtons = buttons;
    unsigned minimized = MinimizedCount();
    if (!capture && minimized && y >= (int32_t)(drawBuffer.height - restoreBarHeight))
    {
        if (pressed)
        {
            unsigned width = drawBuffer.width / minimized;
            if (width > 120) width = 120;

            unsigned index = 0;
            for (size_t i = 0; i < windows.size(); i++)
            {
                auto window = windows[i];
                if (!window->GetMinimised()) continue;

                if ((unsigned)x >= index * width && (unsigned)x < (index + 1) * width)
                {
                    ChangeMode(window, HACKOS_WINDOW_RESTORE);
                    return;
                }

                index++;
            }
        }

        return;
    }

    auto target = capture ? capture : Hit(x, y);

    if (pressed && !capture)
    {
        Focus(target);
        if (!target) return;

        int32_t localX = x - target->GetStartX();
        int32_t localY = y - target->GetStartY();
        if (localY >= 5 && localY < 25 && localX >= (int32_t)target->GetSizeX() - 25 &&
            localX < (int32_t)target->GetSizeX() - 5)
        {
            DestroyWindow(target);
            return;
        }

        if (localY >= 5 && localY < 25)
        {
            if (localX >= (int32_t)target->GetSizeX() - 49 && localX < (int32_t)target->GetSizeX() - 29)
            {
                ChangeMode(target, target->GetMaximised() ? HACKOS_WINDOW_RESTORE : HACKOS_WINDOW_MAXIMIZE);
                return;
            }

            if (localX >= (int32_t)target->GetSizeX() - 73 && localX < (int32_t)target->GetSizeX() - 53)
            {
                ChangeMode(target, HACKOS_WINDOW_MINIMIZE);
                return;
            }
        }

        if (target->GetMaximised() && localY < 30) return;

        capture = target;
        if (!target->GetMaximised() && localX >= (int32_t)target->GetSizeX() - 8 && localY >= (int32_t)target->GetSizeY() - 8)
        {
            captureMode = 3;
            anchorX = x;
            anchorY = y;
            originalWidth = target->GetSizeX();
            originalHeight = target->GetSizeY();
        }
        else if (localY < 30)
        {
            captureMode = 2;
            anchorX = localX;
            anchorY = localY;
        }
        else
        {
            captureMode = 1;
        }
    }

    if (!target) return;

    target->SetMouse(x - target->GetStartX(), y - target->GetStartY() - 30, buttons);

    if (captureMode == 2 && capture)
    {
        int32_t newX = Clamp(x - anchorX, 1, drawBuffer.width - target->GetSizeX() - 1);
        int32_t newY = Clamp(y - anchorY, 1, drawBuffer.height - target->GetSizeY() - 1);
        if (newX != (int32_t)target->GetStartX() || newY != (int32_t)target->GetStartY())
        {
            target->MoveWindow(newX, newY);
            target->SetMouse(x - newX, y - newY - 30, buttons);
            target->SendEvent(HACKOS_WINDOW_MOVE);
        }
    }
    else if (captureMode == 3 && capture)
    {
        int32_t maxWidth = Clamp(drawBuffer.width - target->GetStartX() - 1, 96, 320);
        int32_t maxHeight = Clamp(drawBuffer.height - target->GetStartY() - 1, 60, 270);
        uint32_t width = Clamp((int32_t)originalWidth + x - anchorX, 96, maxWidth);
        uint32_t height = Clamp((int32_t)originalHeight + y - anchorY, 60, maxHeight);
        if (width != target->GetSizeX() || height != target->GetSizeY())
        {
            target->ResizeWindow(width, height);
            target->SendEvent(HACKOS_WINDOW_RESIZE);
        }
    }
    else if (capture || y >= (int32_t)target->GetStartY() + 30)
    {
        target->SendEvent(HACKOS_WINDOW_MOUSE);
    }

    if (!(buttons & 1))
    {
        capture = nullptr;
        captureMode = 0;
    }
}

void WindowManager::Update()
{
    MouseState state;
    unsigned result;
    while ((result = ReadMouse(state)))
    {
        if (state.key)
        {
            if (focused)
            {
                Keyboard(state.key);
            }
            else
            {
                DeliverKeyboard(state.key);
                if (state.key == HACKOS_KEY_INTERRUPT) RequestTerminalInterrupt();
            }

            continue;
        }

        if (result == 2)
        {
            if (!focused) KeyboardOverflow();
            if (capture)
            {
                capture->SetMouse(state.x - capture->GetStartX(), state.y - capture->GetStartY() - 30, state.buttons);
                capture->InputOverflow();
            }

            if (focused && focused != capture)
            {
                focused->SetMouse(state.x - focused->GetStartX(), state.y - focused->GetStartY() - 30, state.buttons);
                focused->InputOverflow();
            }

            capture = nullptr;
            captureMode = 0;
            previousButtons = state.buttons;
        }

        Mouse(state.x, state.y, state.buttons);
        if (state.wheel) Wheel(state.x, state.y, state.wheel);
        Display::SharedDisplay()->dirty = true;
    }
}

void WindowManager::Draw()
{
    for (size_t i = 0; i < windows.size(); i++)
    {
        if (!windows[i]->GetMinimised()) windows[i]->Draw(&drawBuffer);
    }

    DrawRestoreBar();
}

Window* WindowManager::CreateWindow(uint32_t startX, uint32_t startY, uint32_t sizeX, uint32_t sizeY, uint64_t owner)
{
    Window* window = new Window();
    window->SetOwner(owner);
    window->MoveWindow(startX, startY);
    window->ResizeWindow(sizeX, sizeY);

    windows.push_back(window);
    Focus(window);
    return window;
}

Window* WindowManager::GetWindow(uint64_t ID)
{
    for (size_t i = 0; i < windows.size(); i++)
    {
        if (windows[i]->GetWindowID() == ID) return windows[i];
    }

    return nullptr;
}

void WindowManager::DestroyWindow(Window* window)
{
    InterruptGuard guard;
    for (size_t i = 0; i < windows.size(); i++)
    {
        if (windows[i] == window)
        {
            if (capture == window)
            {
                capture = nullptr;
                captureMode = 0;
            }

            bool wasFocused = focused == window;
            if (wasFocused) Focus(nullptr);

            windows.erase(i);
            delete window;

            if (wasFocused) FocusTop();

            Display::SharedDisplay()->dirty = true;
            return;
        }
    }
}

void WindowManager::DestroyWindowByID(uint64_t ID)
{
    auto window = GetWindow(ID);
    if (window) DestroyWindow(window);
}

void WindowManager::FocusTop()
{
    for (size_t i = windows.size(); i > 0; i--)
    {
        if (!windows[i - 1]->GetMinimised())
        {
            Focus(windows[i - 1]);
            return;
        }
    }

    Focus(nullptr);
}

unsigned WindowManager::MinimizedCount() const
{
    unsigned count = 0;
    for (size_t i = 0; i < windows.size(); i++)
    {
        if (windows[i]->GetMinimised()) count++;
    }

    return count;
}

void WindowManager::DrawRestoreBar()
{
    unsigned count = MinimizedCount();
    if (!count) return;

    unsigned width = drawBuffer.width / count;
    if (width > 120) width = 120;

    uint32_t y = drawBuffer.height - restoreBarHeight;
    drawBuffer.DrawRect(0, y, drawBuffer.width, restoreBarHeight, 0xff202020);
    unsigned index = 0;
    auto& console = Display::SharedDisplay()->console;
    uint32_t oldColour = console.fontColour;
    console.fontColour = 0xffffffff;

    for (size_t i = 0; i < windows.size(); i++)
    {
        auto window = windows[i];
        if (!window->GetMinimised()) continue;

        uint32_t x = index * width;
        drawBuffer.DrawRect(x + 1, y + 2, width - 2, restoreBarHeight - 4, 0xff366b96);
        const char* title = window->GetTitle();
        for (unsigned j = 0; title[j] && (j + 1) * console.char_width + 8 < width; j++)
        {
            putchar_at_xy(drawBuffer, title[j], x + 4 + j * console.char_width, y + 6);
        }

        index++;
    }

    console.fontColour = oldColour;
}

uint64_t WindowManager::ChangeMode(Window* window, uint64_t operation)
{
    if (operation == HACKOS_WINDOW_QUERY) return window->Mode();
    if (operation > HACKOS_WINDOW_RESTORE) return HACKOS_ERROR;

    uint32_t oldMode = window->Mode();
    bool geometry = false;

    if (operation == HACKOS_WINDOW_MAXIMIZE && !window->GetMaximised())
    {
        if (drawBuffer.width < 98 || drawBuffer.width > 4096 || drawBuffer.height > 2160 || drawBuffer.height < 90) return HACKOS_ERROR;

        // Reserve every normal window's maximum size as well as maximized
        // buffers. Later normal resizes cannot overrun this 64 MiB budget.
        uint64_t pixels = 16 * 320 * 240;
        uint32_t width = drawBuffer.width - 2;
        uint32_t height = drawBuffer.height - restoreBarHeight - 2;
        pixels += (uint64_t)width * (height - 30);
        for (size_t i = 0; i < windows.size(); i++)
        {
            if (windows[i]->GetMaximised())
            {
                pixels += (uint64_t)windows[i]->GetSizeX() * (windows[i]->GetSizeY() - 30);
            }
        }

        if (pixels > 16 * 1024 * 1024) return HACKOS_ERROR;

        window->SaveNormalGeometry();
        window->MoveWindow(1, 1);
        window->ResizeWindow(width, height);
        window->SetMaximised(true);
        geometry = true;
    }
    else if (operation == HACKOS_WINDOW_RESTORE && !window->GetMinimised() && window->GetMaximised())
    {
        window->RestoreNormalGeometry();
        window->SetMaximised(false);
        geometry = true;
    }

    bool minimize = operation == HACKOS_WINDOW_MINIMIZE;
    window->SetMinimised(minimize);
    window->SetVisible(!minimize);
    if (capture == window)
    {
        capture = nullptr;
        captureMode = 0;
    }

    if (minimize)
    {
        if (focused == window) FocusTop();
    }
    else
    {
        Focus(window);
    }

    if (geometry)
    {
        window->SendEvent(HACKOS_WINDOW_MOVE);
        window->SendEvent(HACKOS_WINDOW_RESIZE);
    }

    if (oldMode != window->Mode()) window->SendEvent(HACKOS_WINDOW_MODE, window->Mode());

    Display::SharedDisplay()->dirty = true;
    return window->Mode();
}

void WindowManager::Wheel(int32_t x, int32_t y, int32_t delta)
{
    if (MinimizedCount() && y >= (int32_t)(drawBuffer.height - restoreBarHeight)) return;
    if (capture && captureMode != 1) return;

    auto target = capture ? capture : Hit(x, y);
    if (!target || target->GetMinimised()) return;
    if (!capture && y < (int32_t)target->GetStartY() + 30) return;

    target->SetMouse(x - target->GetStartX(), y - target->GetStartY() - 30, previousButtons);
    target->SendEvent(HACKOS_WINDOW_WHEEL, (uint32_t)delta);
}
