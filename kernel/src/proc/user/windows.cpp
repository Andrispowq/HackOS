#include "windows.h"

#include "address_space.h"
#include "arch/x86_64/interrupts/guard.h"
#include "proc/tasking/process.h"
#include "drivers/screen/window_manager.h"
#include "drivers/screen/screen.h"

uint64_t user_windows_live;

static int WindowSlot(Process* process, uint64_t handle)
{
    if (!handle) return -1;

    for (int i = 0; i < 4; i++)
    {
        if (process->windows[i] == handle) return i;
    }

    return -1;
}

uint64_t UserWindowCreate(const HackosWindowSpec* source)
{
    auto process = GetRunningProcess();
    HackosWindowSpec spec = {};

    if (!process->userSpace->CopyFromUser(&spec, (uint64_t)source, sizeof(spec))) return HACKOS_ERROR;
    if (!KernelWindowManager || user_windows_live >= 16) return HACKOS_ERROR;

    const auto& screen = Display::SharedDisplay()->framebuffer;
    if (spec.width < 96 || spec.width > 320 || spec.height < 30 || spec.height > 240 ||
        !spec.x || !spec.y || (uint64_t)spec.x + spec.width + 1 > screen.width ||
        (uint64_t)spec.y + spec.height + 31 > screen.height)
    {
        return HACKOS_ERROR;
    }

    unsigned length = 0;
    while (length < 30 && spec.title[length])
    {
        if (spec.title[length] < 32 || spec.title[length] > 126) return HACKOS_ERROR;
        length++;
    }

    if (length == 30) return HACKOS_ERROR;

    for (unsigned i = 0; i < 4; i++)
    {
        if (!process->windows[i])
        {
            auto window = KernelWindowManager->CreateWindow(spec.x, spec.y, spec.width, spec.height + 30, process->pid);
            if (!window) return HACKOS_ERROR;

            window->SetTitle(spec.title);
            process->windows[i] = window->GetWindowID();
            user_windows_live++;
            Display::SharedDisplay()->dirty = true;
            return process->windows[i];
        }
    }

    return HACKOS_ERROR;
}

uint64_t UserWindowBlit(uint64_t handle, const HackosWindowRect* source, const uint32_t* pixels)
{
    auto process = GetRunningProcess();
    if (WindowSlot(process, handle) < 0) return HACKOS_ERROR;

    auto window = KernelWindowManager->GetWindow(handle);
    if (!window) return HACKOS_ERROR;

    HackosWindowRect rect = {};
    if (!process->userSpace->CopyFromUser(&rect, (uint64_t)source, sizeof(rect))) return HACKOS_ERROR;

    auto buffer = window->GetDrawBuffer();
    if (!rect.width || !rect.height || rect.x >= buffer->width || rect.y >= buffer->height ||
        rect.width > buffer->width - rect.x || rect.height > buffer->height - rect.y ||
        (uint64_t)rect.width * rect.height > 1024)
    {
        return HACKOS_ERROR;
    }

    // Validate and copy the entire upload before changing any window pixels.
    // The syscall dispatcher holds a PreemptionGuard throughout this operation.
    uint32_t copy[1024];
    if (!process->userSpace->CopyFromUser(copy, (uint64_t)pixels, (uint64_t)rect.width * rect.height * 4))
    {
        return HACKOS_ERROR;
    }

    for (uint32_t y = 0; y < rect.height; y++)
    {
        for (uint32_t x = 0; x < rect.width; x++)
        {
            buffer->PutPixel(rect.x + x, rect.y + y, copy[y * rect.width + x]);
        }
    }

    Display::SharedDisplay()->dirty = true;
    return 0;
}

uint64_t UserWindowPoll(uint64_t handle)
{
    if (WindowSlot(GetRunningProcess(), handle) < 0) return HACKOS_ERROR;

    return KernelWindowManager->GetWindow(handle) ? 0 : 1;
}

uint64_t UserWindowClose(uint64_t handle)
{
    auto process = GetRunningProcess();
    int slot = WindowSlot(process, handle);
    if (slot < 0) return HACKOS_ERROR;

    KernelWindowManager->DestroyWindowByID(handle);
    process->windows[slot] = 0;
    user_windows_live--;
    Display::SharedDisplay()->dirty = true;
    return 0;
}

void ReleaseUserWindows(Process* process)
{
    for (auto& handle : process->windows)
    {
        if (handle)
        {
            KernelWindowManager->DestroyWindowByID(handle);
            handle = 0;
            user_windows_live--;
            Display::SharedDisplay()->dirty = true;
        }
    }
}

uint64_t UserWindowState(uint64_t handle, HackosWindowState* destination)
{
    auto process = GetRunningProcess();
    if (WindowSlot(process, handle) < 0) return HACKOS_ERROR;

    auto window = KernelWindowManager->GetWindow(handle);
    if (!window) return HACKOS_ERROR;

    auto state = window->State();
    return process->userSpace->CopyToUser((uint64_t)destination, &state, sizeof(state)) ? 0 : HACKOS_ERROR;
}

uint64_t UserWindowEvent(uint64_t handle, HackosWindowEvent* destination)
{
    auto process = GetRunningProcess();
    if (WindowSlot(process, handle) < 0) return HACKOS_ERROR;
    if (!process->userSpace->WritableRange((uint64_t)destination, sizeof(*destination))) return HACKOS_ERROR;

    auto window = KernelWindowManager->GetWindow(handle);
    if (!window) return HACKOS_ERROR;

    HackosWindowEvent event = {};
    if (!window->ReadEvent(event)) return 0;

    process->userSpace->CopyToUser((uint64_t)destination, &event, sizeof(event));
    return 1;
}
