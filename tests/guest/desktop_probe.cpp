#include "hackos/user.h"

int main(int argc, char** argv)
{
    if (argc == 2 && User::Equal(argv[1], "--orphan"))
    {
        HackosWindowSpec spec = {10, 10, 200, 100, "Orphan endpoint"};
        auto window = syscall_window_create(&spec);
        if (window == HACKOS_ERROR)
        {
            return 10;
        }
        auto shell = syscall_terminal_session(HACKOS_TTY_CREATE, window, nullptr);
        if (shell == HACKOS_ERROR || shell == HACKOS_DENIED)
        {
            return 11;
        }
        for (const char* key = "exit\n"; *key; ++key)
        {
            syscall_terminal_session(HACKOS_TTY_INPUT, *key, nullptr);
        }
        if (syscall_wait(shell, nullptr, 0) != shell)
        {
            return 12;
        }
        // With no endpoint foreground owner, these must never signal this
        // probe, which currently owns the independent recovery console.
        syscall_terminal_session(HACKOS_TTY_INPUT, HACKOS_KEY_INTERRUPT, nullptr);
        syscall_terminal_session(HACKOS_TTY_INPUT, HACKOS_KEY_STOP, nullptr);
        syscall_sleep(50);
        User::Print("ORPHAN_SIGNALS_OK\n");
        return 0;
    }
    HackosDesktopEntry entry = {};
    HackosTerminalState state = {};
    if (syscall_desktop(HACKOS_DESKTOP_SCREEN, 0, &entry) != HACKOS_DENIED ||
        syscall_desktop(HACKOS_DESKTOP_FOCUS, 1, nullptr) != HACKOS_DENIED ||
        syscall_desktop(HACKOS_DESKTOP_BACKGROUND, 1, nullptr) != HACKOS_DENIED ||
        syscall_terminal_session(HACKOS_TTY_STATE, 0, &state) != HACKOS_DENIED ||
        syscall_terminal_session(HACKOS_TTY_VIEW, HACKOS_TTY_LIVE, nullptr) != HACKOS_DENIED ||
        syscall_terminal_session(HACKOS_TTY_PASTE, 1, (void*)"x") != HACKOS_DENIED ||
        syscall_terminal_session(HACKOS_TTY_SETTINGS, 0, nullptr) != HACKOS_DENIED ||
        syscall_terminal_session(HACKOS_TTY_INPUT, 'x', nullptr) != HACKOS_DENIED ||
        syscall_terminal_session(HACKOS_TTY_CREATE, 0, nullptr) != HACKOS_DENIED ||
        syscall_session_logout() != HACKOS_DENIED)
    {
        User::Print("DESKTOP_DENIALS_FAILED\n");
        return 1;
    }
    HackosTerminalInfo info = {};
    if (syscall_terminal_info(0, &info) || !(info.flags & HACKOS_TERMINAL_FOREGROUND) || info.columns != 90 ||
        info.rows != 26)
    {
        return 2;
    }
    if (syscall_terminal_set(HACKOS_TERMINAL_FONT_SIZE, 7) != HACKOS_ERROR ||
        syscall_terminal_set(HACKOS_TERMINAL_BG_COLOUR, 0x100000000ULL) != HACKOS_ERROR)
    {
        return 9;
    }
    HackosWindowSpec spec = {10, 10, 200, 100, "Endpoint probe"};
    auto window = syscall_window_create(&spec);
    if (window == HACKOS_ERROR)
    {
        return 3;
    }
    auto shell = syscall_terminal_session(HACKOS_TTY_CREATE, window, nullptr);
    if (shell == HACKOS_ERROR || shell == HACKOS_DENIED)
    {
        return 4;
    }
    if (syscall_terminal_session(HACKOS_TTY_CREATE, window, nullptr) != HACKOS_ERROR ||
        syscall_terminal_session(HACKOS_TTY_STATE, 0, (void*)1) != HACKOS_ERROR ||
        syscall_terminal_session(HACKOS_TTY_CELLS_READ, 0, (void*)1) != HACKOS_ERROR ||
        syscall_terminal_session(HACKOS_TTY_VIEW, HACKOS_TTY_LIVE, (void*)1) != HACKOS_ERROR ||
        syscall_terminal_session(HACKOS_TTY_SETTINGS, 0, (void*)1) != HACKOS_ERROR ||
        syscall_terminal_session(HACKOS_TTY_PASTE, 4097, (void*)1) != HACKOS_ERROR ||
        syscall_terminal_session(HACKOS_TTY_INPUT, 256, nullptr) != HACKOS_ERROR ||
        syscall_terminal_session(HACKOS_TTY_RESIZE, 0 | (5 << 16), nullptr) != HACKOS_ERROR ||
        syscall_terminal_session(HACKOS_TTY_RESIZE, 160 | (61 << 16), nullptr) != HACKOS_ERROR ||
        syscall_terminal_session(HACKOS_TTY_RESIZE, 0x100000000ULL | 80 | (25 << 16), nullptr) != HACKOS_ERROR ||
        syscall_terminal_session(HACKOS_TTY_STATE, 0, &state) != 0 || state.columns != 25 || state.rows != 6)
    {
        return 5;
    }
    if (syscall_terminal_session(HACKOS_TTY_RESIZE, 12 | (1 << 16), nullptr) != 0 ||
        syscall_terminal_session(HACKOS_TTY_STATE, 0, &state) != 0 || state.columns != 12 || state.rows != 1)
    {
        return 6;
    }
    if (syscall_terminal_session(HACKOS_TTY_RESIZE, 160 | (60 << 16), nullptr) != 0 ||
        syscall_terminal_session(HACKOS_TTY_STATE, 0, &state) != 0 || state.columns != 160 || state.rows != 60)
    {
        return 7;
    }
    static HackosTerminalView view;
    if (syscall_terminal_session(HACKOS_TTY_VIEW, HACKOS_TTY_LIVE, &view) || view.firstLine != view.liveLine ||
        view.state.columns != 160 || view.state.rows != 60)
    {
        return 8;
    }
    User::Print("DESKTOP_DENIALS_OK\n");
    return 0;
}
