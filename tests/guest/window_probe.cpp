#include "hackos/user.h"

extern "C" { volatile uint64_t event_gate = 0; }

static bool Check(bool condition)
{
    if (!condition)
    {
        User::Print("PROBE_FAIL: window contract\n");
    }

    return condition;
}

int main(int argc, char** argv)
{
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
        ok = syscall_window_close(handle) == HACKOS_ERROR && ok;
        ok = syscall_window_blit(handle, &rect, &pixel) == HACKOS_ERROR && ok;
        return Check(ok) ? 0 : 1;
    }

    uint64_t handle = syscall_window_create(&spec);
    if (!Check(handle != HACKOS_ERROR)) return 1;

    bool initial = syscall_window_event(handle, (HackosWindowEvent*)0x100000) == HACKOS_ERROR;
    initial = syscall_window_event(handle, (HackosWindowEvent*)0x4000001fffff) == HACKOS_ERROR && initial;
    initial = syscall_window_event(handle, &event) == 1 && event.type == HACKOS_WINDOW_FOCUS && event.focused == 1 && initial;
    initial = syscall_window_event(handle, &event) == 0 && initial;
    initial = syscall_window_state(handle, (HackosWindowState*)0x100000) == HACKOS_ERROR && initial;
    initial = syscall_window_state(handle, &state) == 0 && state.focused == 1 && state.width == 200 && initial;
    if (!Check(initial)) return 1;

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
        if (!Check(ok)) return 1;

        User::Print("WINDOW_EVENTS_OK\n");
        return 0;
    }

    if (argc == 2)
    {
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
    uint64_t fresh = syscall_window_create(&spec);
    ok = fresh != HACKOS_ERROR && fresh != handle && ok;
    ok = syscall_window_close(handle) == HACKOS_ERROR && ok;

    if (!Check(ok)) return 1;

    User::Print("WINDOW_API_OK\n");
    // Leave the fresh handle open to verify normal-exit cleanup.
    return 0;
}
