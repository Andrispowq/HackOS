#include "hackos/gui.h"
#include "hackos/syscall.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            printf("PLATFORM FAIL line %d: %s\n", __LINE__, #x);                                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static unsigned reads, fdCloses, windowCloses, uploads, cancels, presents;
static bool shortFont, badHeader, inputFailure, uploadFailure, presentFailure;
static uint32_t image[64 * 40];
static HackosWindowEvent2 nextEvent;

uint64_t syscall_fd_open(const char*, uint64_t)
{
    reads = 0;
    return 3;
}

uint64_t syscall_fd_read(uint64_t, void* buffer, uint64_t length, uint64_t)
{
    if (reads++ == 0)
    {
        const uint8_t header[] = {0x36, 4, 0, 16};
        memcpy(buffer, header, 4);
        if (badHeader)
        {
            ((uint8_t*)buffer)[0] = 0;
        }
        return 4;
    }
    memset(buffer, 0xff, length);
    return shortFont ? 0 : length;
}

uint64_t syscall_fd_close(uint64_t)
{
    ++fdCloses;
    return 0;
}

uint64_t syscall_window_create(const HackosWindowSpec*)
{
    return 42;
}

uint64_t syscall_window_close(uint64_t)
{
    ++windowCloses;
    return 0;
}

uint64_t syscall_window_input(uint64_t, uint64_t, HackosWindowInputState* state)
{
    *state = {};
    state->focused = 1;
    return inputFailure ? HACKOS_ERROR : 0;
}

uint64_t syscall_window_state(uint64_t, HackosWindowState* state)
{
    *state = {};
    state->width = 64;
    state->height = 40;
    return 0;
}

uint64_t syscall_window_mode(uint64_t, uint64_t)
{
    return 0;
}

uint64_t syscall_window_frame(uint64_t, uint64_t op, HackosWindowFrame* frame)
{
    if (op == HACKOS_WINDOW_BEGIN)
    {
        frame->generation = 1;
        frame->width = 64;
        frame->height = 40;
    }
    if (op == HACKOS_WINDOW_CANCEL)
    {
        ++cancels;
    }
    if (op == HACKOS_WINDOW_PRESENT)
    {
        ++presents;
        return presentFailure ? HACKOS_WINDOW_STALE : 0;
    }
    return 0;
}

uint64_t syscall_window_upload(uint64_t, const HackosWindowUpload* rect, const uint32_t* pixels)
{
    ++uploads;
    if (uploadFailure)
    {
        return HACKOS_WINDOW_STALE;
    }
    if (rect->generation != 1 || rect->width * rect->height > 1024 || rect->x + rect->width > 64 ||
        rect->y + rect->height > 40)
    {
        return HACKOS_ERROR;
    }
    for (unsigned y = 0; y < rect->height; ++y)
    {
        memcpy(image + (rect->y + y) * 64 + rect->x, pixels + y * rect->width, rect->width * 4);
    }
    return 0;
}

uint64_t syscall_window_event2(uint64_t, HackosWindowEvent2* event)
{
    *event = nextEvent;
    nextEvent = {};
    return event->event.type ? 1 : 0;
}

uint64_t syscall_window_close_reply(uint64_t, uint64_t)
{
    return 0;
}

uint64_t syscall_clipboard(uint64_t, void*, uint64_t)
{
    return HACKOS_DENIED;
}

uint64_t syscall_poll(HackosPollFd* fd, uint64_t count, uint64_t timeout)
{
    return count == 1 && fd->fd == 42 && fd->events == (HACKOS_POLL_WINDOW | HACKOS_POLL_READ) && timeout == 123
               ? 0
               : HACKOS_ERROR;
}

int PlatformTests()
{
    Gui::Window window = {};
    Gui::View view;
    view.Init();
    HackosWindowSpec spec = {0, 0, 64, 40, "test"};
    shortFont = true;
    CHECK(!window.Open(spec) && fdCloses == 1 && window.Handle() == 0);
    shortFont = false;
    badHeader = true;
    CHECK(!window.Open(spec) && fdCloses == 2);
    badHeader = false;
    inputFailure = true;
    CHECK(!window.Open(spec) && windowCloses == 1);
    inputFailure = false;
    CHECK(window.Open(spec) && window.Handle() == 42);
    CHECK(!window.Open(spec));
    CHECK(window.Paint(view) && uploads == 3 && presents == 1 && !view.Dirty());
    for (auto pixel : image)
    {
        CHECK(pixel == Gui::DefaultTheme().background);
    }
    uploadFailure = true;
    view.Invalidate();
    CHECK(!window.Paint(view) && cancels == 1 && view.Dirty());
    uploadFailure = false;
    presentFailure = true;
    CHECK(!window.Paint(view) && cancels == 2 && view.Dirty());
    presentFailure = false;
    CHECK(window.Paint(view) && !view.Dirty());
    nextEvent.event.type = HACKOS_WINDOW_OVERFLOW;
    Gui::Action action;
    CHECK(window.Next(view, action) == 1 && view.Dirty() && action.kind == Gui::ActionKind::None);
    CHECK(window.Wait(123) == 0);
    CHECK(window.ReplyClose(false) && window.Handle() == 42);
    CHECK(window.ReplyClose(true) && window.Handle() == 0);
    CHECK(window.Wait(123) == HACKOS_ERROR);
    window.Close();
    CHECK(windowCloses == 1);
    CHECK(window.Open(spec));
    window.Close();
    CHECK(windowCloses == 2);
    puts("GUI platform: font failures, ownership, bounded uploads, cancellation, reset and waits passed");
    return 0;
}
