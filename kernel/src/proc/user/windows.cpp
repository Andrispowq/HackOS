#include "windows.h"

#include "address_space.h"
#include "arch/x86_64/interrupts/guard.h"
#include "proc/tasking/process.h"
#include "drivers/screen/window_manager.h"
#include "drivers/screen/screen.h"

uint64_t user_windows_live;

static int WindowSlot(Process* process, uint64_t handle)
{
    if (!handle)
    {
        return -1;
    }

    for (int i = 0; i < 4; i++)
    {
        if (process->windows[i] == handle)
        {
            return i;
        }
    }

    return -1;
}

uint64_t UserWindowCreate(const HackosWindowSpec* source)
{
    auto process = GetRunningProcess();
    HackosWindowSpec spec = {};

    if (!process->userSpace->CopyFromUser(&spec, (uint64_t)source, sizeof(spec)))
    {
        return HACKOS_ERROR;
    }

    if (!KernelWindowManager || user_windows_live >= 16)
    {
        return HACKOS_ERROR;
    }

    const auto& screen = Display::SharedDisplay()->framebuffer;
    if (spec.width < 96 || spec.width > 4096 || spec.height < 30 || spec.height > 2160 || !spec.x || !spec.y ||
        (uint64_t)spec.x + spec.width + 1 > screen.width || (uint64_t)spec.y + spec.height + 31 > screen.height)
    {
        return HACKOS_ERROR;
    }

    unsigned length = 0;
    while (length < 30 && spec.title[length])
    {
        if (spec.title[length] < 32 || spec.title[length] > 126)
        {
            return HACKOS_ERROR;
        }

        length++;
    }

    if (length == 30)
    {
        return HACKOS_ERROR;
    }

    for (unsigned i = 0; i < 4; i++)
    {
        if (!process->windows[i])
        {
            auto window = KernelWindowManager->CreateWindow(spec.x, spec.y, spec.width, spec.height + 30, process->pid);
            if (!window)
            {
                return HACKOS_ERROR;
            }

            window->SetTitle(spec.title);
            process->windows[i] = window->GetWindowID();
            user_windows_live++;
            Display::SharedDisplay()->Invalidate();
            return process->windows[i];
        }
    }

    return HACKOS_ERROR;
}

uint64_t UserWindowBlit(uint64_t handle, const HackosWindowRect* source, const uint32_t* pixels)
{
    auto process = GetRunningProcess();
    if (WindowSlot(process, handle) < 0)
    {
        return HACKOS_ERROR;
    }

    auto window = KernelWindowManager->GetWindow(handle);
    if (!window)
    {
        return HACKOS_ERROR;
    }

    HackosWindowRect rect = {};
    if (!process->userSpace->CopyFromUser(&rect, (uint64_t)source, sizeof(rect)))
    {
        return HACKOS_ERROR;
    }

    if (window->IsFramed())
    {
        return HACKOS_ERROR;
    }

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

    Framebuffer upload = {copy, rect.width, rect.height, 32, rect.width};
    buffer->CopyFrom(upload, rect.x, rect.y);

    if (!window->GetMinimised())
    {
        Display::SharedDisplay()->InvalidateRect(window->GetStartX() + rect.x, window->GetStartY() + 30 + rect.y,
                                                 rect.width, rect.height);
    }
    return 0;
}

uint64_t UserWindowPoll(uint64_t handle)
{
    if (WindowSlot(GetRunningProcess(), handle) < 0)
    {
        return HACKOS_ERROR;
    }

    return KernelWindowManager->GetWindow(handle) ? 0 : 1;
}

uint64_t UserWindowClose(uint64_t handle)
{
    auto process = GetRunningProcess();
    int slot = WindowSlot(process, handle);
    if (slot < 0)
    {
        return HACKOS_ERROR;
    }

    KernelWindowManager->DestroyWindowByID(handle);
    process->windows[slot] = 0;
    user_windows_live--;
    Display::SharedDisplay()->Invalidate();
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
            Display::SharedDisplay()->Invalidate();
        }
    }
}

uint64_t UserWindowState(uint64_t handle, HackosWindowState* destination)
{
    auto process = GetRunningProcess();
    if (WindowSlot(process, handle) < 0)
    {
        return HACKOS_ERROR;
    }

    auto window = KernelWindowManager->GetWindow(handle);
    if (!window)
    {
        return HACKOS_ERROR;
    }

    auto state = window->State();
    return process->userSpace->CopyToUser((uint64_t)destination, &state, sizeof(state)) ? 0 : HACKOS_ERROR;
}

uint64_t UserWindowEvent(uint64_t handle, HackosWindowEvent* destination)
{
    auto process = GetRunningProcess();
    if (WindowSlot(process, handle) < 0)
    {
        return HACKOS_ERROR;
    }

    if (!process->userSpace->WritableRange((uint64_t)destination, sizeof(*destination)))
    {
        return HACKOS_ERROR;
    }

    auto window = KernelWindowManager->GetWindow(handle);
    if (!window || window->RichInput())
    {
        return HACKOS_ERROR;
    }

    HackosWindowEvent event = {};
    if (!window->ReadEvent(event))
    {
        return 0;
    }

    process->userSpace->CopyToUser((uint64_t)destination, &event, sizeof(event));
    return 1;
}

uint64_t UserWindowMode(uint64_t handle, uint64_t operation)
{
    if (WindowSlot(GetRunningProcess(), handle) < 0)
    {
        return HACKOS_ERROR;
    }

    auto window = KernelWindowManager->GetWindow(handle);
    if (!window)
    {
        return HACKOS_ERROR;
    }

    return KernelWindowManager->ChangeMode(window, operation);
}

uint32_t UserWindowReadiness(uint64_t handle)
{
    if (WindowSlot(GetRunningProcess(), handle) < 0)
    {
        return HACKOS_POLL_INVALID;
    }

    auto window = KernelWindowManager->GetWindow(handle);
    if (!window)
    {
        return HACKOS_POLL_HANGUP;
    }

    return window->EventsReady() ? HACKOS_POLL_READ : 0;
}

uint64_t UserWindowFrame(uint64_t handle, uint64_t operation, HackosWindowFrame* destination)
{
    auto space = GetRunningProcess()->userSpace;
    if (WindowSlot(GetRunningProcess(), handle) < 0 ||
        !space->WritableRange((uint64_t)destination, sizeof(*destination)))
    {
        return HACKOS_ERROR;
    }

    auto window = KernelWindowManager->GetWindow(handle);
    HackosWindowFrame frame = {};
    if (!window || !space->CopyFromUser(&frame, (uint64_t)destination, sizeof(frame)))
    {
        return HACKOS_ERROR;
    }

    uint64_t result = window->Frame(operation, frame);
    if (!result)
    {
        space->CopyToUser((uint64_t)destination, &frame, sizeof(frame));
    }

    return result;
}

uint64_t UserWindowUpload(uint64_t handle, const HackosWindowUpload* source, const uint32_t* pixels)
{
    auto space = GetRunningProcess()->userSpace;
    HackosWindowUpload rect = {};
    if (WindowSlot(GetRunningProcess(), handle) < 0 || !space->CopyFromUser(&rect, (uint64_t)source, sizeof(rect)))
    {
        return HACKOS_ERROR;
    }

    auto window = KernelWindowManager->GetWindow(handle);
    if (!window)
    {
        return HACKOS_ERROR;
    }

    auto buffer = window->UploadBuffer(rect.generation);
    if (!buffer)
    {
        return HACKOS_WINDOW_STALE;
    }

    if (!rect.width || !rect.height || rect.x >= buffer->width || rect.y >= buffer->height ||
        rect.width > buffer->width - rect.x || rect.height > buffer->height - rect.y ||
        (uint64_t)rect.width * rect.height > 1024)
    {
        return HACKOS_ERROR;
    }

    uint32_t copy[1024];
    if (!space->CopyFromUser(copy, (uint64_t)pixels, (uint64_t)rect.width * rect.height * 4))
    {
        return HACKOS_ERROR;
    }

    Framebuffer upload = {copy, rect.width, rect.height, 32, rect.width};
    buffer->CopyFrom(upload, rect.x, rect.y);
    return 0;
}

uint64_t UserWindowInput(uint64_t handle, uint64_t operation, HackosWindowInputState* destination)
{
    auto process = GetRunningProcess();
    if (WindowSlot(process, handle) < 0 || operation > HACKOS_INPUT_ENABLE ||
        !process->userSpace->WritableRange((uint64_t)destination, sizeof(*destination)))
    {
        return HACKOS_ERROR;
    }
    auto window = KernelWindowManager->GetWindow(handle);
    if (!window || (operation == HACKOS_INPUT_QUERY && !window->RichInput()))
    {
        return HACKOS_ERROR;
    }
    if (!window->RichInput())
    {
        window->EnableInput();
    }
    auto state = window->InputState();
    process->userSpace->CopyToUser((uint64_t)destination, &state, sizeof(state));
    return 0;
}

uint64_t UserWindowEvent2(uint64_t handle, HackosWindowEvent2* destination)
{
    auto process = GetRunningProcess();
    if (WindowSlot(process, handle) < 0 ||
        !process->userSpace->WritableRange((uint64_t)destination, sizeof(*destination)))
    {
        return HACKOS_ERROR;
    }
    auto window = KernelWindowManager->GetWindow(handle);
    if (!window || !window->RichInput())
    {
        return HACKOS_ERROR;
    }
    HackosWindowEvent2 event = {};
    if (!window->ReadEvent2(event))
    {
        return 0;
    }
    process->userSpace->CopyToUser((uint64_t)destination, &event, sizeof(event));
    return 1;
}

uint64_t UserWindowCloseReply(uint64_t handle, uint64_t accept)
{
    if (WindowSlot(GetRunningProcess(), handle) < 0 || accept > 1)
    {
        return HACKOS_ERROR;
    }
    auto window = KernelWindowManager->GetWindow(handle);
    if (!window || !window->RichInput() || !window->ClosePending())
    {
        return HACKOS_ERROR;
    }
    if (accept)
    {
        return UserWindowClose(handle);
    }
    window->RejectClose();
    return 0;
}

static uint64_t clipboardSession, clipboardLength;
static uint8_t clipboardData[HACKOS_CLIPBOARD_LIMIT];

void ClearSessionClipboard(uint64_t session)
{
    if (clipboardSession == session)
    {
        for (auto& byte : clipboardData)
        {
            byte = 0;
        }
        clipboardLength = clipboardSession = 0;
    }
}

uint64_t UserClipboard(uint64_t operation, void* buffer, uint64_t length)
{
    auto process = GetRunningProcess();
    if (!process->sessionId || !KernelWindowManager || KernelWindowManager->FocusedOwner() != process->pid)
    {
        return HACKOS_DENIED;
    }
    if (operation > HACKOS_CLIPBOARD_WRITE || length > HACKOS_CLIPBOARD_LIMIT)
    {
        return HACKOS_ERROR;
    }
    uint64_t available = clipboardSession == process->sessionId ? clipboardLength : 0;
    if (operation == HACKOS_CLIPBOARD_QUERY)
    {
        return !buffer && !length ? available : HACKOS_ERROR;
    }
    if (operation == HACKOS_CLIPBOARD_READ)
    {
        if (length < available)
        {
            return HACKOS_BUFFER_SMALL;
        }
        if (!available)
        {
            return 0;
        }
        return process->userSpace->CopyToUser((uint64_t)buffer, clipboardData, available) ? available : HACKOS_ERROR;
    }
    uint8_t copy[HACKOS_CLIPBOARD_LIMIT];
    if (length && !process->userSpace->CopyFromUser(copy, (uint64_t)buffer, length))
    {
        return HACKOS_ERROR;
    }
    for (uint64_t i = 0; i < HACKOS_CLIPBOARD_LIMIT; ++i)
    {
        clipboardData[i] = i < length ? copy[i] : 0;
    }
    clipboardLength = length;
    clipboardSession = process->sessionId;
    return length;
}
