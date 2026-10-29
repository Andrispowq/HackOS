#include "hackos/gui.h"
#include "hackos/syscall.h"

namespace Gui
{
static uint64_t ReadClipboard(void*, void* data, uint64_t length)
{
    return syscall_clipboard(HACKOS_CLIPBOARD_READ, data, length);
}

static uint64_t WriteClipboard(void*, const void* data, uint64_t length)
{
    return syscall_clipboard(HACKOS_CLIPBOARD_WRITE, (void*)data, length);
}

bool Window::Open(const HackosWindowSpec& spec)
{
    // Call only on a new/closed adapter. A zero handle represents no ownership.
    if (handle)
    {
        return false;
    }
    uint64_t fd = syscall_fd_open("~/UTIL/FONTS/zap-light16.psf", HACKOS_OPEN_READ);
    if (fd >= HACKOS_FD_LIMIT)
    {
        return false;
    }
    uint8_t header[4];
    bool valid = syscall_fd_read(fd, header, 4, 0) == 4 && header[0] == 0x36 && header[1] == 4 && !(header[2] & ~3U) &&
                 header[3] == 16;
    for (unsigned i = 0; valid && i < sizeof(font.glyphs); i += 1024)
    {
        valid = syscall_fd_read(fd, font.glyphs + i, 1024, 0) == 1024;
    }
    syscall_fd_close(fd);
    if (!valid)
    {
        return false;
    }
    uint64_t created = syscall_window_create(&spec);
    if (created == HACKOS_ERROR)
    {
        return false;
    }
    HackosWindowInputState state;
    if (syscall_window_input(created, HACKOS_INPUT_ENABLE, &state) != 0)
    {
        syscall_window_close(created);
        return false;
    }
    handle = created;
    return true;
}

uint64_t Window::Handle() const
{
    return handle;
}

bool Window::Paint(View& view)
{
    if (!handle)
    {
        return false;
    }
    if (syscall_window_mode(handle, HACKOS_WINDOW_QUERY) & HACKOS_WINDOW_MINIMIZED)
    {
        return true;
    }
    HackosWindowFrame frame = {};
    if (syscall_window_frame(handle, HACKOS_WINDOW_BEGIN, &frame) != 0)
    {
        return false;
    }
    view.Layout(frame.width, frame.height);
    uint32_t pixels[1024];
    Canvas canvas;
    // Wide strips reduce syscall count without allocating a full client buffer.
    unsigned stripWidth = frame.width < 1024 ? frame.width : 1024;
    unsigned stripHeight = 1024 / stripWidth;
    for (unsigned y = 0; y < frame.height; y += stripHeight)
    {
        unsigned h = frame.height - y < stripHeight ? frame.height - y : stripHeight;
        for (unsigned x = 0; x < frame.width; x += stripWidth)
        {
            unsigned w = frame.width - x < stripWidth ? frame.width - x : stripWidth;
            canvas.Init(pixels, w, {(int)x, (int)y, (int)w, (int)h});
            view.Draw(canvas, font);
            HackosWindowUpload upload = {frame.generation, x, y, w, h};
            if (syscall_window_upload(handle, &upload, pixels) != 0)
            {
                syscall_window_frame(handle, HACKOS_WINDOW_CANCEL, &frame);
                return false;
            }
        }
    }
    if (syscall_window_frame(handle, HACKOS_WINDOW_PRESENT, &frame) != 0)
    {
        syscall_window_frame(handle, HACKOS_WINDOW_CANCEL, &frame);
        return false;
    }
    view.Painted();
    return true;
}

uint64_t Window::Next(View& view, Action& action)
{
    action = {ActionKind::None, NoWidget, 0};
    if (!handle)
    {
        return HACKOS_ERROR;
    }
    HackosWindowEvent2 event = {};
    uint64_t result = syscall_window_event2(handle, &event);
    if (result != 1)
    {
        return result;
    }
    if (event.event.type == HACKOS_WINDOW_OVERFLOW || event.event.type == HACKOS_WINDOW_INPUT_RESET)
    {
        HackosWindowInputState input;
        HackosWindowState state;
        if (syscall_window_input(handle, HACKOS_INPUT_QUERY, &input) != 0 || syscall_window_state(handle, &state) != 0)
        {
            return HACKOS_ERROR;
        }
        view.Layout(state.width, state.height);
        view.ResetInput(input.focused);
        return 1;
    }
    if (event.event.type == HACKOS_WINDOW_MODE)
    {
        view.Invalidate();
    }
    HackosClock clock = {};
    // The current PIT contract is 1000 Hz: clock ticks are milliseconds.
    syscall_clock(&clock);
    action = view.Handle(event, {nullptr, ReadClipboard, WriteClipboard}, clock.ticks);
    return 1;
}

uint64_t Window::Wait(uint64_t timeout)
{
    if (!handle)
    {
        return HACKOS_ERROR;
    }
    HackosPollFd poll = {handle, HACKOS_POLL_WINDOW | HACKOS_POLL_READ, 0};
    return syscall_poll(&poll, 1, timeout);
}

bool Window::ReplyClose(bool accept)
{
    if (!handle || syscall_window_close_reply(handle, accept) != 0)
    {
        return false;
    }
    if (accept)
    {
        handle = 0;
    }
    return true;
}

void Window::Close()
{
    if (handle)
    {
        syscall_window_close(handle);
        handle = 0;
    }
}
}
