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
    if (value < minimum)
    {
        return minimum;
    }

    if (value > maximum)
    {
        return maximum;
    }

    return value;
}

WindowManager::WindowManager()
{
    drawBuffer = Display::SharedDisplay()->composition;
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
    if (focused == window)
    {
        return;
    }

    if (capture)
    {
        suppressedButtons |= previousButtons;
        capture = nullptr;
        captureMode = 0;
    }
    if (focused)
    {
        focused->SetFocused(false);
    }

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

        focused->SetFocused(true, keyModifiers);
    }

    Display::SharedDisplay()->Invalidate();
}

void WindowManager::RequestClose(Window* window)
{
    if (window->RichInput())
    {
        window->RequestClose();
    }
    else
    {
        DestroyWindow(window);
    }
}

void WindowManager::CycleFocus(bool reverse)
{
    Window* next = nullptr;
    Window* wrap = nullptr;
    uint64_t current = focused ? focused->GetWindowID() : (reverse ? ~0ULL : 0);
    // Creation order stays stable while raising changes z-order.
    for (auto& slot : slots)
    {
        uint64_t id = slot.GetWindowID();
        if (!id)
        {
            continue;
        }
        if (!wrap || (reverse ? id > wrap->GetWindowID() : id < wrap->GetWindowID()))
        {
            wrap = &slot;
        }
        if ((reverse ? id < current : id > current) &&
            (!next || (reverse ? id > next->GetWindowID() : id < next->GetWindowID())))
        {
            next = &slot;
        }
    }
    if (!next)
    {
        next = wrap;
    }
    if (next)
    {
        if (next->GetMinimised())
        {
            ChangeMode(next, HACKOS_WINDOW_RESTORE);
        }
        else
        {
            Focus(next);
        }
    }
}

void WindowManager::Keyboard(const GuiKey& key)
{
    keyModifiers = key.modifiers;
    uint64_t bit = 1ULL << (key.key % 64);
    auto& consumed = shortcutKeys[key.key / 64];
    if (consumed & bit)
    {
        if (!key.down)
        {
            consumed &= ~bit;
        }
        return;
    }
    bool alt = key.modifiers & HACKOS_MOD_ALT;
    bool control = key.modifiers & HACKOS_MOD_CTRL;
    bool shortcut = (alt && !control &&
                     (key.key == 0x0f || key.key == 0x01 || key.key == 0x3e || key.key == 0x43 || key.key == 0x44)) ||
                    (alt && control && key.key == 0x01);
    if (key.down && shortcut)
    {
        consumed |= bit;
        if ((key.flags & HACKOS_KEY_REPEAT) && !control)
        {
            return;
        }
        if (control)
        {
            if (focused)
            {
                RequestUserInterrupt(focused->GetOwner());
            }
        }
        else if (key.key == 0x0f)
        {
            CycleFocus(key.modifiers & HACKOS_MOD_SHIFT);
        }
        else if (key.key == 0x01)
        {
            Focus(panel);
        }
        else if (focused && key.key == 0x3e)
        {
            RequestClose(focused);
        }
        else if (focused && key.key == 0x43)
        {
            ChangeMode(focused, HACKOS_WINDOW_MINIMIZE);
        }
        else if (focused && key.key == 0x44)
        {
            ChangeMode(focused, focused->GetMaximised() ? HACKOS_WINDOW_RESTORE : HACKOS_WINDOW_MAXIMIZE);
        }
        return;
    }
    if (focused && focused->RichInput())
    {
        focused->Key(key);
        return;
    }
    if (!key.down || !key.decoded || alt)
    {
        return;
    }
    if (focused)
    {
        if (key.decoded == HACKOS_KEY_INTERRUPT)
        {
            RequestUserInterrupt(focused->GetOwner());
        }
        else
        {
            focused->SendEvent(HACKOS_WINDOW_KEY, key.decoded);
        }
    }
    else
    {
        DeliverKeyboard(key.decoded);
        if (key.decoded == HACKOS_KEY_INTERRUPT)
        {
            RequestTerminalInterrupt();
        }
        else if (key.decoded == HACKOS_KEY_STOP)
        {
            RequestTerminalStop();
        }
    }
}

Window* WindowManager::Hit(int32_t x, int32_t y)
{
    if (panel && y >= (int32_t)panel->GetStartY())
    {
        return panel;
    }
    for (size_t i = windows.size(); i > 0; i--)
    {
        auto window = windows[i - 1];
        if (window->GetMinimised())
        {
            continue;
        }

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
    suppressedButtons &= buttons;
    buttons &= ~suppressedButtons;
    bool pressed = (buttons & 1) && !(previousButtons & 1);
    previousButtons = buttons;
    unsigned minimized = MinimizedCount();
    if (!panel && !capture && minimized && y >= (int32_t)(drawBuffer.height - restoreBarHeight))
    {
        if (pressed)
        {
            unsigned width = drawBuffer.width / minimized;
            if (width > 120)
            {
                width = 120;
            }

            unsigned index = 0;
            for (size_t i = 0; i < windows.size(); i++)
            {
                auto window = windows[i];
                if (!window->GetMinimised())
                {
                    continue;
                }

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
        if (!target)
        {
            return;
        }

        if (target == panel)
        {
            capture = panel;
            captureMode = 1;
            panel->SetMouse(x - panel->GetStartX(), y - panel->GetStartY() - 30, buttons);
            panel->SendEvent(HACKOS_WINDOW_MOUSE);
            return;
        }
        int32_t localX = x - target->GetStartX();
        int32_t localY = y - target->GetStartY();
        if (localY >= 5 && localY < 25 && localX >= (int32_t)target->GetSizeX() - 25 &&
            localX < (int32_t)target->GetSizeX() - 5)
        {
            RequestClose(target);
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

        if (target->GetMaximised() && localY < 30)
        {
            return;
        }

        capture = target;
        if (!target->GetMaximised() && localX >= (int32_t)target->GetSizeX() - 8 &&
            localY >= (int32_t)target->GetSizeY() - 8)
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

    if (!target)
    {
        return;
    }

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
        int32_t maxWidth = Clamp(drawBuffer.width - target->GetStartX() - 1, 96, 4096);
        int32_t maxHeight = Clamp(drawBuffer.height - target->GetStartY() - 1, 60, 2190);
        uint32_t width = Clamp((int32_t)originalWidth + x - anchorX, 96, maxWidth);
        uint32_t height = Clamp((int32_t)originalHeight + y - anchorY, 60, maxHeight);
        if (width != target->GetSizeX() || height != target->GetSizeY())
        {
            if (target->ResizeWindow(width, height))
            {
                target->SendEvent(HACKOS_WINDOW_RESIZE);
            }
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
        if (state.keyboard)
        {
            if (result == 3)
            {
                // Recovery preserves terminal interruption/force termination,
                // never invents a GUI copy gesture or a held key after loss.
                if (state.input.key == HACKOS_PHYSICAL_ESCAPE)
                {
                    if (focused)
                    {
                        RequestUserInterrupt(focused->GetOwner());
                    }
                }
                else if (!focused || !focused->RichInput())
                {
                    Keyboard(state.input);
                }
                keyModifiers = KeyboardModifiers();
            }
            else
            {
                Keyboard(state.input);
            }
            continue;
        }

        if (result == 2)
        {
            if (!focused)
            {
                KeyboardOverflow();
            }

            keyModifiers = KeyboardModifiers();
            for (auto& keys : shortcutKeys)
            {
                keys = 0;
            }
            for (auto& slot : slots)
            {
                if (!slot.GetWindowID())
                {
                    continue;
                }
                if (slot.RichInput())
                {
                    slot.ResetInput(slot.GetWindowID() == (focused ? focused->GetWindowID() : 0) ? keyModifiers : 0);
                }
                slot.SetMouse(state.x - slot.GetStartX(), state.y - slot.GetStartY() - 30, 0);
                slot.InputOverflow();
            }
            capture = nullptr;
            captureMode = 0;
            previousButtons = 0;
            suppressedButtons = state.buttons;
            Display::SharedDisplay()->cursorDirty = true;
            continue;
        }

        Mouse(state.x, state.y, state.buttons);
        if (state.wheel)
        {
            Wheel(state.x, state.y, state.wheel);
        }

        Display::SharedDisplay()->cursorDirty = true;
    }
}

void WindowManager::Draw()
{
    for (size_t i = 0; i < windows.size(); i++)
    {
        if (windows[i] != panel && !windows[i]->GetMinimised())
        {
            windows[i]->Draw(&drawBuffer);
        }
    }

    if (panel)
    {
        panel->Draw(&drawBuffer);
    }
    else
    {
        DrawRestoreBar();
    }
}

Window* WindowManager::CreateWindow(uint32_t startX, uint32_t startY, uint32_t sizeX, uint32_t sizeY, uint64_t owner)
{
    Window* window = nullptr;
    for (auto& slot : slots)
    {
        if (!slot.GetWindowID())
        {
            window = &slot;
            break;
        }
    }

    if (!window || !window->Initialise(sizeX, sizeY))
    {
        return nullptr;
    }

    window->SetOwner(owner);
    window->MoveWindow(startX, startY);

    windows.push_back(window);
    Focus(window);
    return window;
}

Window* WindowManager::GetWindow(uint64_t ID)
{
    for (size_t i = 0; i < windows.size(); i++)
    {
        if (windows[i]->GetWindowID() == ID)
        {
            return windows[i];
        }
    }

    return nullptr;
}

void WindowManager::DestroyWindow(Window* window)
{
    if (panel == window)
    {
        panel = nullptr;
    }
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
            if (wasFocused)
            {
                Focus(nullptr);
            }

            windows.erase(i);
            window->Release();
            WakeStreamWaiters();

            if (wasFocused)
            {
                FocusTop();
            }

            Display::SharedDisplay()->Invalidate();
            return;
        }
    }
}

void WindowManager::DestroyWindowByID(uint64_t ID)
{
    auto window = GetWindow(ID);
    if (window)
    {
        DestroyWindow(window);
    }
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
        if (windows[i]->GetMinimised())
        {
            count++;
        }
    }

    return count;
}

void WindowManager::DrawRestoreBar()
{
    unsigned count = MinimizedCount();
    if (!count)
    {
        return;
    }

    unsigned width = drawBuffer.width / count;
    if (width > 120)
    {
        width = 120;
    }

    uint32_t y = drawBuffer.height - restoreBarHeight;
    drawBuffer.DrawRect(0, y, drawBuffer.width, restoreBarHeight, 0xff202020);
    unsigned index = 0;

    for (size_t i = 0; i < windows.size(); i++)
    {
        auto window = windows[i];
        if (!window->GetMinimised())
        {
            continue;
        }

        uint32_t x = index * width;
        drawBuffer.DrawRect(x + 1, y + 2, width - 2, restoreBarHeight - 4, 0xff366b96);
        const char* title = window->GetTitle();
        for (unsigned j = 0; title[j] && (j + 1) * 8 + 8 < width; j++)
        {
            putchar_at_xy(drawBuffer, title[j], x + 4 + j * 8, y + 6);
        }

        index++;
    }
}

uint64_t WindowManager::ChangeMode(Window* window, uint64_t operation)
{
    if (window == panel && operation != HACKOS_WINDOW_QUERY)
    {
        return HACKOS_DENIED;
    }
    if (operation == HACKOS_WINDOW_QUERY)
    {
        return window->Mode();
    }

    if (operation > HACKOS_WINDOW_RESTORE)
    {
        return HACKOS_ERROR;
    }

    uint32_t oldMode = window->Mode();
    bool geometry = false;

    if (operation == HACKOS_WINDOW_MAXIMIZE && !window->GetMaximised())
    {
        if (drawBuffer.width < 98 || drawBuffer.width > 4096 || drawBuffer.height > 2160 || drawBuffer.height < 90)
        {
            return HACKOS_ERROR;
        }

        uint32_t width = drawBuffer.width - 2;
        uint32_t height = drawBuffer.height - (panel ? panel->GetSizeY() + 2 : restoreBarHeight) - 2;
        // Capture normal geometry before resize; failed allocation leaves it visible.
        window->SaveNormalGeometry();
        if (!window->ResizeWindow(width, height))
        {
            return HACKOS_ERROR;
        }

        window->MoveWindow(1, 1);
        window->SetMaximised(true);
        geometry = true;
    }
    else if (operation == HACKOS_WINDOW_RESTORE && !window->GetMinimised() && window->GetMaximised())
    {
        if (!window->RestoreNormalGeometry())
        {
            return HACKOS_ERROR;
        }
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
        if (focused == window)
        {
            FocusTop();
        }
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

    if (oldMode != window->Mode())
    {
        window->SendEvent(HACKOS_WINDOW_MODE, window->Mode());
    }

    Display::SharedDisplay()->Invalidate();
    return window->Mode();
}

void WindowManager::Wheel(int32_t x, int32_t y, int32_t delta)
{
    if (MinimizedCount() && y >= (int32_t)(drawBuffer.height - restoreBarHeight))
    {
        return;
    }

    if (capture && captureMode != 1)
    {
        return;
    }

    auto target = capture ? capture : Hit(x, y);
    if (!target || target->GetMinimised())
    {
        return;
    }

    if (!capture && y < (int32_t)target->GetStartY() + 30)
    {
        return;
    }

    target->SetMouse(x - target->GetStartX(), y - target->GetStartY() - 30, previousButtons);
    target->SendEvent(HACKOS_WINDOW_WHEEL, (uint32_t)delta);
}

void WindowManager::SetPanel(Window* window)
{
    panel = window;
    window->SetDesktopPanel();
    window->SetResizable(false);
    Display::SharedDisplay()->Invalidate();
}

uint64_t WindowManager::Activate(Window* window)
{
    if (window->GetMinimised())
    {
        return ChangeMode(window, HACKOS_WINDOW_RESTORE);
    }
    Focus(window);
    return 0;
}
