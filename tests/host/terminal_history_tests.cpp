#include "support.hpp"
#include "hackos/runtime/terminal_history.h"
#include "hackos/runtime/terminal_view.h"

TEST(terminal_history_wrap_snapshot_and_clear)
{
    TerminalHistory history = {};
    HackosTerminalState state = {};
    state.columns = 5;
    state.rows = 3;
    char live[15] = {'L', 'I', 'V', 'E', ' ', 'N', 'E', 'X', 'T', ' ', 'E', 'N', 'D', ' ', ' '};
    for (unsigned i = 0; i < 700; ++i)
    {
        char row[2] = {(char)(i % 127 + 1), (char)(i / 127 + 1)};
        history.Push(row, 2);
    }
    HackosTerminalView view = {};
    history.Snapshot(view, state, live, 0);
    CHECK(view.firstLine == 188 && view.oldestLine == 188 && view.liveLine == 700);
    CHECK(view.cells[0] == (char)(188 % 127 + 1) && view.cells[1] == 2);
    CHECK(view.cells[2] == ' ' && view.cells[4] == ' ');
    history.Snapshot(view, state, live, 699);
    CHECK(view.cells[0] == (char)(699 % 127 + 1) && view.cells[5] == 'L' && view.cells[10] == 'N');
    history.Snapshot(view, state, live, HACKOS_TTY_LIVE);
    CHECK(view.firstLine == 700 && view.cells[0] == 'L' && view.cells[10] == 'E');
    history.Clear();
    CHECK(history.count == 0 && history.next == 0 && history.epoch == 1);
    for (const auto& row : history.lines)
    {
        for (char ch : row)
        {
            CHECK(ch == 0);
        }
    }
    history.Snapshot(view, state, live, 699);
    CHECK(view.firstLine == 0 && view.epoch == 1 && view.cells[0] == 'L');
    return 0;
}

TEST(terminal_view_scroll_selection_and_reset)
{
    TerminalViewport viewport = {};
    viewport.Init();
    HackosTerminalView view = {};
    view.state.columns = 5;
    view.state.rows = 3;
    view.firstLine = view.liveLine = 700;
    view.oldestLine = 188;
    const char* text = "one  two  three";
    for (unsigned i = 0; i < 15; ++i)
    {
        view.cells[i] = text[i];
    }
    CHECK(viewport.Accept(view));
    viewport.Scroll(3);
    viewport.Scroll(3);
    CHECK(viewport.requested == 694);
    viewport.Scroll(9999);
    CHECK(viewport.requested == 188);
    viewport.Scroll(-9999);
    CHECK(viewport.requested == HACKOS_TTY_LIVE);
    CHECK(viewport.Mouse(32, 32, 1));
    CHECK(viewport.Mouse(0, 0, 0));
    char copy[32] = {};
    CHECK(viewport.Copy(copy, sizeof(copy)) == 13);
    CHECK(test_equal(copy, "one\ntwo\nthree", 13));
    CHECK(viewport.Selected(0) && viewport.Selected(14) && !viewport.Selected(15));
    view.cells[0] = 'X';
    CHECK(!viewport.Accept(view) && viewport.image.cells[0] == 'o');
    viewport.ResetInput(1);
    viewport.Live();
    CHECK(!viewport.Mouse(8, 0, 1) && !viewport.selected);
    viewport.Mouse(8, 0, 0);
    CHECK(viewport.Mouse(8, 0, 1));
    CHECK(viewport.Mouse(-100, -100, 0));
    CHECK(viewport.Copy(copy, sizeof(copy)) == 2 && test_equal(copy, "on", 2));
    ++view.epoch;
    CHECK(viewport.Accept(view) && !viewport.selected && !viewport.dragging);
    return 0;
}

TEST(terminal_selection_clipboard_limit)
{
    TerminalViewport viewport = {};
    viewport.Init();
    HackosTerminalView view = {};
    view.state.columns = 160;
    view.state.rows = 60;
    for (auto& ch : view.cells)
    {
        ch = 'x';
    }
    viewport.Accept(view);
    viewport.Mouse(0, 0, 1);
    viewport.Mouse(159 * 8, 59 * 16, 0);
    char copy[HACKOS_CLIPBOARD_LIMIT];
    CHECK(viewport.Copy(copy, sizeof(copy)) == HACKOS_BUFFER_SMALL);
    viewport.cursor = 4070; // 4071 characters plus 25 newlines = 4096.
    CHECK(viewport.Copy(copy, sizeof(copy)) == 4096);
    viewport.cursor++;
    CHECK(viewport.Copy(copy, sizeof(copy)) == HACKOS_BUFFER_SMALL);
    viewport.Live();
    CHECK(viewport.Copy(copy, sizeof(copy)) == HACKOS_ERROR);
    return 0;
}
