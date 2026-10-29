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

#include "hackos/runtime/terminal_paste.h"
#include "hackos/runtime/terminal_appearance.h"

TEST(terminal_paste_atomic_normalization_and_bounds)
{
    TerminalPaste paste = {};
    CHECK(paste.Set("ab\r\ncd\nef\tgh\r", 13));
    CHECK(!paste.Set("replacement", 11));
    const char* expected = "ab cd ef gh ";
    char ch = 0;
    for (unsigned i = 0; i < 12; ++i)
    {
        CHECK(paste.Read(ch) && ch == expected[i]);
    }
    CHECK(!paste.Read(ch));
    CHECK(!paste.Set("bad\033text", 8));
    CHECK(!paste.Set("\200", 1));
    CHECK(!paste.Pending());
    char full[HACKOS_CLIPBOARD_LIMIT];
    for (auto& byte : full)
    {
        byte = 'x';
    }
    CHECK(!paste.Set(full, sizeof(full) + 1));
    CHECK(paste.Set(full, sizeof(full)));
    for (unsigned i = 0; i < sizeof(full); ++i)
    {
        CHECK(paste.Read(ch) && ch == 'x');
    }
    CHECK(!paste.Pending());
    CHECK(paste.Set("secret", 6));
    paste.Clear();
    for (char byte : paste.bytes)
    {
        CHECK(byte == 0);
    }
    CHECK(!paste.Pending());
    return 0;
}

TEST(terminal_appearance_scaling_colour_and_selection)
{
    TerminalAppearance appearance = {};
    appearance.Reset();
    CHECK(appearance.height == 16 && appearance.background == 0x101b25ff);
    CHECK(!appearance.Configure(HACKOS_TERMINAL_FONT_SIZE, 7));
    CHECK(!appearance.Configure(HACKOS_TERMINAL_FONT_SIZE, 65));
    CHECK(!appearance.Configure(HACKOS_TERMINAL_FONT_SIZE, 31));
    CHECK(!appearance.Configure(HACKOS_TERMINAL_BG_COLOUR, 0x100000000ULL));
    CHECK(appearance.height == 16 && appearance.background == 0x101b25ff);
    CHECK(TerminalBlend(0x123456ff, 0xff000000) == 0xff123456);
    CHECK(TerminalBlend(0xabcdef00, 0xff123456) == 0xff123456);
    CHECK(TerminalBlend(0xffffff80, 0xff000000) == 0xff808080);
    uint8_t glyphs[4096] = {};
    for (unsigned row = 0; row < 16; ++row)
    {
        glyphs[255 * 16 + row] = 0x80 >> (row % 8);
    }
    for (unsigned height = 8; height <= 64; height += 2)
    {
        CHECK(appearance.Configure(HACKOS_TERMINAL_FONT_SIZE, height));
        for (unsigned y = 0; y < height; ++y)
        {
            for (unsigned x = 0; x < height / 2; ++x)
            {
                CHECK(TerminalGlyphPixel(glyphs, 255, x, y, height) == (x * 8 / (height / 2) == (y * 16 / height) % 8));
            }
        }
        TerminalViewport viewport = {};
        viewport.Init();
        HackosTerminalView view = {};
        view.state.columns = 10;
        view.state.rows = 5;
        viewport.Accept(view);
        CHECK(viewport.Mouse(height + 1, height + 1, 1, height / 2, height));
        CHECK(viewport.anchor == 12);
        viewport.Mouse(99999, 99999, 0, height / 2, height);
        CHECK(viewport.cursor == 49);
    }
    CHECK(appearance.Configure(HACKOS_TERMINAL_DEFAULT, HACKOS_TERMINAL_FONT_SIZE));
    CHECK(appearance.height == 16);
    CHECK(!appearance.Configure(HACKOS_TERMINAL_DEFAULT, 0));
    return 0;
}
