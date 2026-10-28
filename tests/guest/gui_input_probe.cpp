#include "hackos/user.h"
#include "hackos/stdio.h"
using namespace User;
extern "C"
{
    volatile uint64_t gui_gate = 0;
}
#define CHECK(value)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(value))                                                                                                  \
        {                                                                                                              \
            printf("PROBE_FAIL GUI %u\n", __LINE__);                                                                   \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main(int argc, char** argv)
{
    if (argc == 2 && Equal(argv[1], "lock"))
    {
        CHECK(syscall_fd_open("/tmp/gui-notes.txt", HACKOS_OPEN_WRITE | HACKOS_OPEN_CREATE) < HACKOS_FD_LIMIT);
        Print("GUI_LOCKED\n");
        syscall_poll(nullptr, 0, HACKOS_WAIT_FOREVER);
        return 1;
    }
    if (argc == 3 && Equal(argv[1], "foreign"))
    {
        uint64_t handle = 0;
        for (unsigned i = 0; argv[2][i]; ++i)
        {
            handle = handle * 10 + argv[2][i] - '0';
        }
        HackosWindowInputState state;
        HackosWindowEvent2 event;
        CHECK(syscall_window_input(handle, HACKOS_INPUT_ENABLE, &state) == HACKOS_ERROR);
        CHECK(syscall_window_event2(handle, &event) == HACKOS_ERROR);
        CHECK(syscall_window_close_reply(handle, 1) == HACKOS_ERROR);
        CHECK(syscall_clipboard(HACKOS_CLIPBOARD_QUERY, nullptr, 0) == HACKOS_DENIED);
        CHECK(syscall_clipboard(HACKOS_CLIPBOARD_WRITE, (void*)"bad", 3) == HACKOS_DENIED);
        return 0;
    }
    HackosWindowSpec spec = {100, 100, 240, 170, "GUI input probe"};
    uint64_t handle = syscall_window_create(&spec);
    CHECK(handle != HACKOS_ERROR);
    HackosWindowInputState state;
    HackosWindowEvent2 event;
    CHECK(syscall_window_input(handle, HACKOS_INPUT_ENABLE, (HackosWindowInputState*)0x100000) == HACKOS_ERROR);
    CHECK(syscall_window_event2(handle, &event) == HACKOS_ERROR);
    CHECK(syscall_window_input(handle, HACKOS_INPUT_ENABLE, &state) == 0);
    CHECK(state.focused == 1 && !state.buttons && !state.closePending);
    CHECK(syscall_window_input(handle, 99, &state) == HACKOS_ERROR);
    CHECK(syscall_window_event2(handle, (HackosWindowEvent2*)0x100000) == HACKOS_ERROR);
    CHECK(syscall_window_event2(handle, &event) == 1 && event.event.type == HACKOS_WINDOW_INPUT_RESET);
    HackosWindowEvent old;
    CHECK(syscall_window_event(handle, &old) == HACKOS_ERROR);
    CHECK(syscall_window_close_reply(handle, 1) == HACKOS_ERROR);
    if (argc == 2 && Equal(argv[1], "empty"))
    {
        CHECK(syscall_clipboard(HACKOS_CLIPBOARD_QUERY, nullptr, 0) == 0);
        CHECK(syscall_window_close(handle) == 0);
        Print("GUI_EMPTY_OK\n");
        return 0;
    }
    static char data[4096], copy[4096];
    for (unsigned i = 0; i < sizeof(data); ++i)
    {
        data[i] = (char)(i % 251);
    }
    CHECK(syscall_clipboard(HACKOS_CLIPBOARD_WRITE, data, 4096) == 4096);
    CHECK(syscall_clipboard(HACKOS_CLIPBOARD_WRITE, data, 4097) == HACKOS_ERROR);
    CHECK(syscall_clipboard(HACKOS_CLIPBOARD_WRITE, (void*)0x100000, 10) == HACKOS_ERROR);
    CHECK(syscall_clipboard(HACKOS_CLIPBOARD_QUERY, nullptr, 0) == 4096);
    CHECK(syscall_clipboard(HACKOS_CLIPBOARD_READ, copy, 4095) == HACKOS_BUFFER_SMALL);
    CHECK(syscall_clipboard(HACKOS_CLIPBOARD_READ, (void*)0x100000, 4096) == HACKOS_ERROR);
    CHECK(syscall_clipboard(HACKOS_CLIPBOARD_READ, copy, 4096) == 4096);
    for (unsigned i = 0; i < sizeof(data); ++i)
    {
        CHECK(data[i] == copy[i]);
    }
    CHECK(syscall_clipboard(HACKOS_CLIPBOARD_WRITE, (void*)"shared", 6) == 6);
    char number[24];
    unsigned digits = 0;
    for (uint64_t value = handle; value; value /= 10)
    {
        number[digits++] = '0' + value % 10;
    }
    number[digits] = 0;
    for (unsigned i = 0; i < digits / 2; ++i)
    {
        char ch = number[i];
        number[i] = number[digits - i - 1];
        number[digits - i - 1] = ch;
    }
    const char* args[] = {"~/USR/BIN/GUIPROBE", "foreign", number};
    uint64_t child = syscall_spawn_args(args[0], 3, args);
    int64_t status;
    CHECK(syscall_wait(child, &status, 0) == child && status == 0);
    if (argc == 2 && Equal(argv[1], "overflow"))
    {
        Print("GUI_OVERFLOW_READY\n");
        while (!gui_gate)
        {
            syscall_sleep(10);
        }
        CHECK(syscall_window_event2(handle, &event) == 1 && event.event.type == HACKOS_WINDOW_OVERFLOW);
        CHECK(syscall_window_input(handle, HACKOS_INPUT_QUERY, &state) == 0 && state.closePending);
        CHECK(!state.modifiers && !state.buttons);
        for (auto keys : state.keys)
        {
            CHECK(keys == 0);
        }
        CHECK(syscall_window_event2(handle, &event) == 1 && event.event.type == HACKOS_WINDOW_CLOSE_REQUEST);
        CHECK(syscall_window_close_reply(handle, 1) == 0);
        Print("GUI_OVERFLOW_OK\n");
        return 0;
    }
    if (argc == 2 && Equal(argv[1], "order"))
    {
        Print("GUI_ORDER_READY\n");
        while (!gui_gate)
        {
            syscall_sleep(10);
        }
        bool text = false, close = false;
        while (syscall_window_event2(handle, &event) == 1)
        {
            if (event.event.type == HACKOS_WINDOW_TEXT && event.event.key == 'a')
            {
                CHECK(!close);
                text = true;
            }
            if (event.event.type == HACKOS_WINDOW_CLOSE_REQUEST)
            {
                CHECK(text);
                close = true;
            }
        }
        CHECK(close && syscall_window_close_reply(handle, 1) == 0);
        Print("GUI_ORDER_OK\n");
        return 0;
    }
    Print("GUI_READY\n");
    unsigned closes = 0;
    for (;;)
    {
        HackosPollFd ready = {handle, HACKOS_POLL_WINDOW | HACKOS_POLL_READ, 0};
        CHECK(syscall_poll(&ready, 1, HACKOS_WAIT_FOREVER) == 1 && ready.revents == HACKOS_POLL_READ);
        while (syscall_window_event2(handle, &event) == 1)
        {
            CHECK(syscall_window_input(handle, HACKOS_INPUT_QUERY, &state) == 0);
            unsigned type = event.event.type;
            if (type == HACKOS_WINDOW_KEY_DOWN)
            {
                printf("GUI_DOWN %u %u %u\n", event.event.key, event.modifiers, event.flags);
            }
            if (type == HACKOS_WINDOW_KEY_UP)
            {
                printf("GUI_UP %u %u\n", event.event.key, event.modifiers);
            }
            if (type == HACKOS_WINDOW_TEXT)
            {
                printf("GUI_TEXT %u\n", event.event.key);
            }
            if (type == HACKOS_WINDOW_INPUT_RESET || type == HACKOS_WINDOW_OVERFLOW)
            {
                printf("GUI_RESET %u\n", type);
                if (!state.focused)
                {
                    for (auto keys : state.keys)
                    {
                        CHECK(!keys);
                    }
                    CHECK(!state.buttons && !state.modifiers);
                    CHECK(syscall_clipboard(HACKOS_CLIPBOARD_QUERY, nullptr, 0) == HACKOS_DENIED);
                }
            }
            if (type == HACKOS_WINDOW_CLOSE_REQUEST)
            {
                CHECK(state.closePending);
                CHECK(syscall_window_close_reply(handle, 9) == HACKOS_ERROR);
                if (!closes++)
                {
                    CHECK(syscall_window_close_reply(handle, 0) == 0);
                    CHECK(syscall_window_input(handle, HACKOS_INPUT_QUERY, &state) == 0 && !state.closePending);
                    Print("GUI_CLOSE_REJECTED\n");
                }
                else
                {
                    CHECK(syscall_window_close_reply(handle, 1) == 0);
                    Print("GUI_DONE\n");
                    return 0;
                }
            }
        }
    }
}
