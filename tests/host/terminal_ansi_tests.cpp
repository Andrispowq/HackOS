#include "support.hpp"
#include "hackos/runtime/terminal_screen.h"
#include "hackos/runtime/terminal_view.h"
#include "hackos/runtime/terminal_appearance.h"

static void Send(TerminalScreen& screen, const char* text)
{
    for (; *text; ++text)
    {
        screen.Feed((uint8_t)*text);
    }
}

TEST(terminal_ansi_cursor_erase_and_fragmented_sgr)
{
    TerminalScreen screen = {};
    screen.columns = 10;
    screen.rows = 4;
    screen.Clear();
    Send(screen, "abc\033[");
    CHECK(screen.x == 3 && screen.mode == 2);
    Send(screen, "31;44;1;4mR");
    CHECK(screen.cells[3] == 'R');
    CHECK(screen.styles[3] == (2 | (5 << 5) | HACKOS_TTY_BOLD | HACKOS_TTY_UNDERLINE));
    Send(screen, "\033[0mZ\033[2;3HXY\033[s\033[999D!\033[u?");
    CHECK(screen.cells[12] == 'X' && screen.cells[13] == 'Y' && screen.cells[14] == '?');
    CHECK(screen.cells[10] == '!' && screen.styles[4] == 0);
    Send(screen, "\033[2;3H\033[1K");
    CHECK(screen.cells[10] == ' ' && screen.cells[12] == ' ' && screen.cells[13] == 'Y');
    Send(screen, "\033[0J");
    CHECK(screen.cells[13] == ' ' && screen.cells[0] == 'a');
    Send(screen, "\033[2J");
    CHECK(screen.x == 2 && screen.y == 1 && screen.cells[0] == ' ');
    Send(screen, "\033[999B\033[999C");
    CHECK(screen.x == 9 && screen.y == 3);
    Send(screen, "\033[999A\033[999D\033[0;0H");
    CHECK(screen.x == 0 && screen.y == 0);
    Send(screen, "\033[?25l");
    CHECK(!screen.Caret(true));
    Send(screen, "\033[?25h");
    CHECK(screen.Caret(false));
    return 0;
}

TEST(terminal_ansi_styles_history_and_frozen_copy)
{
    TerminalScreen screen = {};
    screen.columns = 8;
    screen.rows = 2;
    screen.Clear();
    for (unsigned line = 0; line < 600; ++line)
    {
        Send(screen, line % 2 ? "\033[32mGREEN\n" : "\033[31mRED\n");
    }
    CHECK(screen.history.count == 512);
    HackosTerminalStyledView snapshot = {};
    HackosTerminalState state = {};
    state.columns = screen.columns;
    state.rows = screen.rows;
    screen.history.Snapshot(snapshot.view, state, screen.cells, 0);
    screen.history.SnapshotStyles(snapshot.styles, snapshot.view, screen.styles);
    CHECK(snapshot.view.firstLine == 87 && snapshot.view.cells[0] == 'G' && snapshot.styles[0] == 3);
    CHECK(snapshot.view.cells[8] == 'R' && snapshot.styles[8] == 2);
    TerminalViewport viewport = {};
    viewport.Init();
    viewport.Accept(snapshot.view, snapshot.styles);
    viewport.Mouse(0, 0, 1);
    viewport.Mouse(32, 0, 0);
    snapshot.styles[0] = 9;
    CHECK(!viewport.Accept(snapshot.view, snapshot.styles) && viewport.styles[0] == 3);
    char copied[16];
    CHECK(viewport.Copy(copied, sizeof(copied)) == 5 && test_equal(copied, "GREEN", 5));
    Send(screen, "\033[3J");
    CHECK(screen.history.count == 0 && screen.cells[0] == 'G');
    screen.Clear();
    CHECK(screen.style == 0 && screen.styles[0] == 0 && screen.history.styles[0][0] == 0);
    return 0;
}

TEST(terminal_ansi_unsupported_malformed_and_recovery)
{
    TerminalScreen screen = {};
    screen.columns = 80;
    screen.rows = 25;
    screen.Clear();
    Send(screen, "\033[38;2;255;0;0mA\033[48;5;1mB");
    CHECK(screen.styles[0] == 0 && screen.styles[1] == 0);
    Send(screen, "\033]52;c;secret\aC\033Ppayload\033\\D\033[?1049hE");
    CHECK(test_equal(screen.cells, "ABCDE", 5));
    Send(screen, "\033 G\033(B\033[9999999999999999999CF\033[1:2mG");
    CHECK(test_equal(screen.cells, "ABCDEFG", 7));
    Send(screen, "\033[");
    for (unsigned i = 0; i < 10000; ++i)
    {
        screen.Feed(';');
    }
    Send(screen, "mH\033[31\030I");
    CHECK(test_equal(screen.cells, "ABCDEFGHI", 9));
    Send(screen, "\033]unterminated");
    for (unsigned i = 0; i < 10000; ++i)
    {
        screen.Feed('x');
    }
    CHECK(screen.x == 9);
    Send(screen, "\033\\J\033[31m\033c");
    CHECK(screen.x == 0 && screen.y == 0 && screen.style == 0 && screen.mode == 0);
    CHECK(TerminalAnsiColour(2, 0, true) == 0xffff5555);
    CHECK(TerminalAnsiColour(0, 0xff123456) == 0xff123456);
    return 0;
}

TEST(terminal_ansi_random_bounded_input)
{
    TerminalScreen screen = {};
    screen.columns = 1;
    screen.rows = 1;
    screen.Clear();
    uint32_t random = 0x1a5b;
    for (unsigned i = 0; i < 100000; ++i)
    {
        random = random * 1664525 + 1013904223;
        screen.Feed((uint8_t)(random >> 24));
        CHECK(screen.x < screen.columns && screen.y < screen.rows && screen.count <= 16 && screen.bytes <= 128);
        if (i % 1000 == 0)
        {
            screen.columns = i % 160 + 1;
            screen.rows = i % 60 + 1;
            screen.Clear();
        }
    }
    return 0;
}
