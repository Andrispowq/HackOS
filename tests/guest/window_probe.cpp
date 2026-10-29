#include "hackos/user.h"

extern "C"
{
    volatile uint64_t event_gate = 0;
}

static bool Check(bool condition)
{
    if (!condition)
    {
        User::Print("PROBE_FAIL: window contract\n");
    }

    return condition;
}

static void Gate(uint64_t value, const char* message)
{
    User::Print(message);
    while (event_gate < value)
    {
        syscall_sleep(10);
    }
}

static int Frames()
{
    HackosWindowSpec spec = {100, 100, 640, 480, "Frame probe"};
    uint64_t handle = syscall_window_create(&spec);
    if (!Check(handle != HACKOS_ERROR))
    {
        return 1;
    }
    HackosWindowEvent event;
    while (syscall_window_event(handle, &event) == 1)
    {
    }
    HackosPollFd items[2] = {{handle, HACKOS_POLL_WINDOW | HACKOS_POLL_READ, 0}, {HACKOS_STDOUT, HACKOS_POLL_WRITE, 0}};
    bool ok = syscall_poll(items, 1, 20) == 0;
    ok = syscall_poll(items, 2, HACKOS_WAIT_FOREVER) == 1 && items[1].revents == HACKOS_POLL_WRITE && ok;
    items[0].events |= HACKOS_POLL_WRITE;
    ok = syscall_poll(items, 1, 0) == HACKOS_ERROR && ok;
    items[0].events = HACKOS_POLL_WINDOW | HACKOS_POLL_READ;
    HackosWindowFrame frame = {};
    ok = syscall_window_frame(handle, HACKOS_WINDOW_BEGIN, (HackosWindowFrame*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_window_frame(handle, HACKOS_WINDOW_BEGIN, &frame) == 0 && frame.width == 640 && frame.height == 480 &&
         ok;
    HackosWindowUpload upload = {frame.generation, 0, 0, 1, 1};
    uint32_t pixel = 0xff112233;
    ok = syscall_window_upload(handle, &upload, &pixel) == 0 && ok;
    ok = syscall_window_upload(handle, &upload, (uint32_t*)0x100000) == HACKOS_ERROR && ok;
    HackosWindowUpload bad = {frame.generation, 0xffffffff, 0, 1, 1};
    ok = syscall_window_upload(handle, &bad, &pixel) == HACKOS_ERROR && ok;
    bad = {frame.generation, 0, 0, 640, 2};
    ok = syscall_window_upload(handle, &bad, &pixel) == HACKOS_ERROR && ok;
    upload.x = 2;
    uint32_t retained = 0xffabcdef;
    ok = syscall_window_upload(handle, &upload, &retained) == 0 && ok;
    upload.x = 0;
    ok = syscall_window_frame(handle, HACKOS_WINDOW_PRESENT, &frame) == 0 && ok;
    ok = syscall_window_upload(handle, &upload, &pixel) == HACKOS_WINDOW_STALE && ok;
    HackosWindowRect legacy = {0, 0, 1, 1};
    ok = syscall_window_blit(handle, &legacy, &pixel) == HACKOS_ERROR && ok;
    ok = syscall_window_frame(handle, HACKOS_WINDOW_BEGIN_RETAIN, &frame) == 0 && ok;
    upload.generation = frame.generation;
    pixel = 0xff445566;
    ok = syscall_window_upload(handle, &upload, &pixel) == 0 && ok;
    if (!Check(ok))
    {
        return 1;
    }
    Gate(1, "FRAME_STAGED\n");
    ok = syscall_window_frame(handle, HACKOS_WINDOW_PRESENT, &frame) == 0;
    ok = syscall_window_frame(handle, HACKOS_WINDOW_BEGIN, &frame) == 0 && ok;
    upload.generation = frame.generation;
    ok = syscall_window_mode(handle, HACKOS_WINDOW_MAXIMIZE) == HACKOS_WINDOW_MAXIMIZED && ok;
    ok = syscall_window_upload(handle, &upload, &pixel) == HACKOS_WINDOW_STALE && ok;
    ok = syscall_window_frame(handle, HACKOS_WINDOW_PRESENT, &frame) == HACKOS_WINDOW_STALE && ok;
    if (!Check(ok))
    {
        return 1;
    }
    Gate(2, "FRAME_RESIZED\n");
    ok = syscall_window_frame(handle, HACKOS_WINDOW_BEGIN, &frame) == 0;
    HackosWindowFrame old = frame;
    ok = syscall_window_frame(handle, HACKOS_WINDOW_BEGIN, &frame) == 0 && frame.generation != old.generation && ok;
    ok = syscall_window_frame(handle, HACKOS_WINDOW_CANCEL, &old) == HACKOS_WINDOW_STALE && ok;
    ok = syscall_window_frame(handle, HACKOS_WINDOW_CANCEL, &frame) == 0 && ok;
    if (!Check(ok))
    {
        return 1;
    }
    Gate(3, "FRAME_PRESSURE_READY\n");
    ok = syscall_window_frame(handle, HACKOS_WINDOW_BEGIN, &frame) == HACKOS_ERROR;
    ok = syscall_window_mode(handle, HACKOS_WINDOW_RESTORE) == HACKOS_ERROR && ok;
    HackosWindowState state;
    ok = syscall_window_state(handle, &state) == 0 && state.x == 1 && state.width > 640 && ok;
    if (!Check(ok))
    {
        return 1;
    }
    Gate(4, "FRAME_PRESSURE_OK\n");
    // Repeated physical failures must not consume quota or mutate the retained frame.
    for (unsigned failure = 0; failure < 4; ++failure)
    {
        ok = syscall_window_frame(handle, HACKOS_WINDOW_BEGIN_RETAIN, &frame) == HACKOS_ERROR;
        ok = syscall_window_mode(handle, HACKOS_WINDOW_RESTORE) == HACKOS_ERROR && ok;
        ok = syscall_window_state(handle, &state) == 0 && state.x == 1 && state.width > 640 && ok;
        if (!Check(ok))
        {
            return 1;
        }
    }
    Gate(5, "FRAME_ALLOCATION_OK\n");
    ok = syscall_window_frame(handle, HACKOS_WINDOW_BEGIN, &frame) == 0;
    ok = syscall_window_frame(handle, HACKOS_WINDOW_PRESENT, &frame) == 0 && ok;
    while (syscall_window_event(handle, &event) == 1)
    {
    }
    if (!Check(ok))
    {
        return 1;
    }
    User::Print("FRAME_WAITING\n");
    ok = syscall_poll(items, 1, HACKOS_WAIT_FOREVER) == 1 && items[0].revents == HACKOS_POLL_READ;
    ok = syscall_window_event(handle, &event) == 1 && event.type == HACKOS_WINDOW_KEY && event.key == 'z' && ok;
    if (!Check(ok))
    {
        return 1;
    }
    User::Print("FRAME_CLOSE_WAIT\n");
    do
    {
        ok = syscall_poll(items, 1, HACKOS_WAIT_FOREVER) == 1 && ok;
        while (syscall_window_event(handle, &event) == 1)
        {
        }
    } while (ok && items[0].revents == HACKOS_POLL_READ);
    ok = items[0].revents == HACKOS_POLL_HANGUP && ok;
    ok = syscall_window_close(handle) == 0 && ok;
    ok = syscall_poll(items, 1, 0) == 1 && items[0].revents == HACKOS_POLL_INVALID && ok;
    if (!Check(ok))
    {
        return 1;
    }
    User::Print("WINDOW_FRAMES_OK\n");
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 2 && User::Equal(argv[1], "frames"))
    {
        return Frames();
    }
    HackosWindowSpec spec = {100, 100, 200, 170, "Window probe"};
    HackosWindowRect rect = {0, 0, 1, 1};
    uint32_t pixel = 0xffabcdef;
    HackosWindowState state;
    HackosWindowEvent event;

    if (argc == 3 && User::Equal(argv[1], "foreign"))
    {
        uint64_t handle = 0;
        for (unsigned i = 0; argv[2][i]; i++)
        {
            handle = handle * 10 + argv[2][i] - '0';
        }

        bool ok = syscall_window_state(handle, &state) == HACKOS_ERROR;
        ok = syscall_window_event(handle, &event) == HACKOS_ERROR && ok;
        ok = syscall_window_poll(handle) == HACKOS_ERROR && ok;
        ok = syscall_window_mode(handle, HACKOS_WINDOW_MAXIMIZE) == HACKOS_ERROR && ok;
        ok = syscall_window_close(handle) == HACKOS_ERROR && ok;
        ok = syscall_window_blit(handle, &rect, &pixel) == HACKOS_ERROR && ok;
        HackosWindowFrame frame = {};
        HackosWindowUpload upload = {};
        ok = syscall_window_frame(handle, HACKOS_WINDOW_BEGIN, &frame) == HACKOS_ERROR && ok;
        ok = syscall_window_upload(handle, &upload, &pixel) == HACKOS_ERROR && ok;
        HackosPollFd poll = {handle, HACKOS_POLL_WINDOW | HACKOS_POLL_READ, 0};
        ok = syscall_poll(&poll, 1, 0) == 1 && poll.revents == HACKOS_POLL_INVALID && ok;
        return Check(ok) ? 0 : 1;
    }

    uint64_t handle = syscall_window_create(&spec);
    if (!Check(handle != HACKOS_ERROR))
    {
        return 1;
    }

    bool initial = syscall_window_event(handle, (HackosWindowEvent*)0x100000) == HACKOS_ERROR;
    initial = syscall_window_event(handle, (HackosWindowEvent*)0x4000001fffff) == HACKOS_ERROR && initial;
    initial =
        syscall_window_event(handle, &event) == 1 && event.type == HACKOS_WINDOW_FOCUS && event.focused == 1 && initial;
    initial = syscall_window_event(handle, &event) == 0 && initial;
    initial = syscall_window_state(handle, (HackosWindowState*)0x100000) == HACKOS_ERROR && initial;
    initial = syscall_window_state(handle, &state) == 0 && state.focused == 1 && state.width == 200 && initial;
    if (!Check(initial))
    {
        return 1;
    }

    if (argc == 2 && User::Equal(argv[1], "events"))
    {
        User::Print("WINDOW_EVENTS_READY\n");
        while (!event_gate)
        {
            syscall_yield();
        }

        bool ok = syscall_window_event(handle, (HackosWindowEvent*)0x100000) == HACKOS_ERROR;
        ok = syscall_window_event(handle, &event) == 1 && event.type == HACKOS_WINDOW_OVERFLOW && ok;
        ok = syscall_window_event(handle, &event) == 0 && ok;
        ok = syscall_window_state(handle, &state) == 0 && state.focused == 1 && ok;
        if (!Check(ok))
        {
            return 1;
        }

        User::Print("WINDOW_EVENTS_OK\n");
        return 0;
    }

    if (argc == 2)
    {
        HackosWindowFrame pending = {};
        if (!Check(syscall_window_frame(handle, HACKOS_WINDOW_BEGIN, &pending) == 0))
        {
            return 1;
        }
        if (User::Equal(argv[1], "fault") || User::Equal(argv[1], "minimized"))
        {
            if (!Check(syscall_window_mode(handle, HACKOS_WINDOW_MINIMIZE) == HACKOS_WINDOW_MINIMIZED))
            {
                return 1;
            }
        }

        if (User::Equal(argv[1], "fault"))
        {
            *(volatile uint64_t*)0x100000 = 0;
        }

        User::Print("WINDOW_HELD\n");
        while (true)
        {
            syscall_yield();
        }
    }

    bool ok = syscall_window_create((const HackosWindowSpec*)0x100000) == HACKOS_ERROR;
    ok = syscall_window_create((const HackosWindowSpec*)0x4000001fffff) == HACKOS_ERROR && ok;
    spec.width = 0xffffffff;
    ok = syscall_window_create(&spec) == HACKOS_ERROR && ok;
    spec.width = 200;
    spec.x = 0xffffffff;
    ok = syscall_window_create(&spec) == HACKOS_ERROR && ok;
    spec.x = 100;
    spec.height = 0;
    ok = syscall_window_create(&spec) == HACKOS_ERROR && ok;
    spec.height = 170;
    for (auto& c : spec.title)
    {
        c = 'x';
    }

    ok = syscall_window_create(&spec) == HACKOS_ERROR && ok;
    spec.title[0] = 0;

    uint64_t extra[3];
    for (unsigned cycle = 0; cycle < 32; ++cycle)
    {
        for (auto& window : extra)
        {
            window = syscall_window_create(&spec);
            ok = window != HACKOS_ERROR && ok;
        }

        ok = syscall_window_create(&spec) == HACKOS_ERROR && ok;
        for (auto window : extra)
        {
            ok = syscall_window_close(window) == 0 && ok;
            ok = syscall_window_close(window) == HACKOS_ERROR && ok;
        }
    }

    ok = syscall_window_poll(handle) == 0 && ok;
    ok = syscall_window_blit(handle, &rect, &pixel) == 0 && ok;
    ok = syscall_window_blit(handle, &rect, (const uint32_t*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_window_blit(handle, &rect, (const uint32_t*)0x4000001ffffe) == HACKOS_ERROR && ok;
    ok = syscall_window_blit(handle, (const HackosWindowRect*)0x100000, &pixel) == HACKOS_ERROR && ok;
    rect.x = 0xffffffff;
    ok = syscall_window_blit(handle, &rect, &pixel) == HACKOS_ERROR && ok;
    rect = {0, 0, 200, 170};
    ok = syscall_window_blit(handle, &rect, &pixel) == HACKOS_ERROR && ok;
    rect = {199, 169, 2, 2};
    ok = syscall_window_blit(handle, &rect, &pixel) == HACKOS_ERROR && ok;
    rect = {0, 0, 0, 1};
    ok = syscall_window_blit(handle, &rect, &pixel) == HACKOS_ERROR && ok;

    ok = syscall_window_mode(handle, 99) == HACKOS_ERROR && ok;
    ok = syscall_window_mode(handle, HACKOS_WINDOW_QUERY) == 0 && ok;
    ok = syscall_window_mode(handle, HACKOS_WINDOW_MINIMIZE) == HACKOS_WINDOW_MINIMIZED && ok;
    ok = syscall_window_state(handle, &state) == 0 && state.focused == 0 && ok;
    ok = syscall_window_mode(handle, HACKOS_WINDOW_RESTORE) == 0 && ok;
    ok = syscall_window_mode(handle, HACKOS_WINDOW_MAXIMIZE) == HACKOS_WINDOW_MAXIMIZED && ok;
    ok = syscall_window_state(handle, &state) == 0 && state.x == 1 && state.width > 320 && ok;
    ok = syscall_window_mode(handle, HACKOS_WINDOW_MINIMIZE) == (HACKOS_WINDOW_MINIMIZED | HACKOS_WINDOW_MAXIMIZED) &&
         ok;
    ok = syscall_window_mode(handle, HACKOS_WINDOW_RESTORE) == HACKOS_WINDOW_MAXIMIZED && ok;
    ok = syscall_window_mode(handle, HACKOS_WINDOW_RESTORE) == 0 && ok;
    ok = syscall_window_state(handle, &state) == 0 && state.x == 100 && state.y == 100 && state.width == 200 &&
         state.height == 170 && ok;

    char number[21] = {};
    unsigned digits = 0;
    uint64_t value = handle;
    do
    {
        number[digits++] = '0' + value % 10;
        value /= 10;
    } while (value);
    for (unsigned i = 0; i < digits / 2; i++)
    {
        char c = number[i];
        number[i] = number[digits - i - 1];
        number[digits - i - 1] = c;
    }

    const char* args[] = {"~/USR/BIN/WINPROBE", "foreign", number};
    uint64_t child = syscall_spawn_args(args[0], 3, args);
    int64_t status = -1;
    ok = syscall_wait(child, &status, 0) == child && status == 0 && ok;
    ok = syscall_window_poll(handle) == 0 && ok;
    ok = syscall_window_close(handle) == 0 && ok;
    ok = syscall_window_poll(handle) == HACKOS_ERROR && ok;
    ok = syscall_window_mode(handle, HACKOS_WINDOW_RESTORE) == HACKOS_ERROR && ok;
    uint64_t fresh = syscall_window_create(&spec);
    ok = fresh != HACKOS_ERROR && fresh != handle && ok;
    ok = syscall_window_close(handle) == HACKOS_ERROR && ok;

    if (!Check(ok))
    {
        return 1;
    }

    User::Print("WINDOW_API_OK\n");
    if (!Check(syscall_window_mode(fresh, HACKOS_WINDOW_MINIMIZE) == HACKOS_WINDOW_MINIMIZED))
    {
        return 1;
    }

    // Leave the fresh minimized handle open to verify normal-exit cleanup.
    return 0;
}
