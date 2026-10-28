#include "hackos/gui.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
using namespace Gui;
#define CHECK(x)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            printf("FAIL line %d: %s\n", __LINE__, #x);                                                                \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static View view;
static Font font;
static uint32_t buffer[1026];
static unsigned tests;

static HackosWindowEvent2 Key(unsigned key, unsigned mods = 0, unsigned flags = 0)
{
    HackosWindowEvent2 e = {};
    e.event.type = HACKOS_WINDOW_KEY_DOWN;
    e.event.key = key;
    e.modifiers = mods;
    e.flags = flags;
    return e;
}

static HackosWindowEvent2 Text(char ch)
{
    HackosWindowEvent2 e = {};
    e.event.type = HACKOS_WINDOW_TEXT;
    e.event.key = ch;
    return e;
}

static HackosWindowEvent2 Mouse(int x, int y, unsigned buttons)
{
    HackosWindowEvent2 e = {};
    e.event.type = HACKOS_WINDOW_MOUSE;
    e.event.x = x;
    e.event.y = y;
    e.event.buttons = buttons;
    return e;
}

static char clipboard[4096];
static unsigned clipboardLength;
static bool clipboardFail;

static uint64_t Read(void*, void* data, uint64_t size)
{
    if (clipboardFail)
    {
        return HACKOS_DENIED;
    }
    if (clipboardLength > size)
    {
        return HACKOS_BUFFER_SMALL;
    }
    memcpy(data, clipboard, clipboardLength);
    return clipboardLength;
}

static uint64_t Write(void*, const void* data, uint64_t size)
{
    if (clipboardFail)
    {
        return HACKOS_DENIED;
    }
    memcpy(clipboard, data, size);
    clipboardLength = size;
    return size;
}

static Clipboard Clip = {nullptr, Read, Write};

static int Drawing()
{
    Canvas c;
    for (auto& p : buffer)
    {
        p = 0xdeadbeef;
    }
    c.Init(buffer + 1, 32, {10, 20, 32, 32});
    c.SetClip({15, 25, 5, 5});
    c.Fill({INT_MIN, INT_MIN, INT_MAX, INT_MAX}, 1);
    c.Fill({0, 0, INT_MAX, INT_MAX}, 7);
    CHECK(buffer[0] == 0xdeadbeef && buffer[1025] == 0xdeadbeef);
    for (unsigned y = 0; y < 32; ++y)
    {
        for (unsigned x = 0; x < 32; ++x)
        {
            CHECK(buffer[1 + y * 32 + x] == (x >= 5 && x < 10 && y >= 5 && y < 10 ? 7 : 0xdeadbeef));
        }
    }
    ++tests;
    c.SetClip({10, 20, 32, 32});
    c.Border({INT_MAX, INT_MAX, INT_MAX, INT_MAX}, 8);
    c.Init(buffer + 1, 1, {0, 0, 32, 32});
    c.Fill({0, 0, 32, 32}, 9);
    CHECK(buffer[1] == 0xdeadbeef);
    c.Init(buffer + 1, 32, {INT_MAX, INT_MAX, 32, 32});
    c.Fill({INT_MAX, INT_MAX, 32, 32}, 9);
    CHECK(buffer[0] == 0xdeadbeef && buffer[1025] == 0xdeadbeef);
    ++tests;
    memset(font.glyphs, 0xff, sizeof(font.glyphs));
    c.Init(buffer + 1, 32, {0, 0, 32, 32});
    c.Fill({0, 0, 32, 32}, 0);
    c.SetClip({4, 4, 4, 4});
    c.Text(font, 0, 0, "A", 3);
    CHECK(buffer[1 + 4 * 32 + 4] == 3 && buffer[1] == 0 && buffer[1 + 8 * 32 + 4] == 0);
    c.Text(font, INT_MAX, INT_MAX, "A\nB", 3);
    c.Text(font, INT_MIN, INT_MIN, "A", 3);
    CHECK(Canvas::Measure("ab\nc").width == 16 && Canvas::Measure("ab\nc").height == 32);
    ++tests;
    return 0;
}

static int Layout()
{
    view.Init();
    view.SetLayout(0, 0, 0, 0, 0, 0);
    unsigned row = view.Add(Kind::Row, 0);
    view.SetLayout(row, 0, 0, 1, 0, 4);
    unsigned a = view.Add(Kind::Button, row, "A"), b = view.Add(Kind::Button, row, "B"),
             c = view.Add(Kind::Button, row, "C");
    view.SetLayout(a, 40, 0);
    view.SetLayout(b, 0, 0, 1);
    view.SetLayout(c, 0, 0, 2);
    view.Layout(300, 100);
    CHECK(view.Get(a)->bounds.width == 40 && view.Get(b)->bounds.width == 84 && view.Get(c)->bounds.width == 168);
    CHECK(view.Get(c)->bounds.x + view.Get(c)->bounds.width == 300);
    ++tests;
    view.Layout(20, 10);
    CHECK(view.Get(a)->clip.width == 20 && view.Get(b)->clip.width == 0);
    view.Layout(0, 0);
    CHECK(view.Get(a)->clip.width == 0);
    ++tests;
    view.SetVisible(a, false);
    view.Layout(300, 100);
    CHECK(view.Get(b)->bounds.x == 0);
    view.SetEnabled(row, false);
    CHECK(view.Focus() == NoWidget);
    CHECK(!view.SetFocus(b));
    view.SetEnabled(row, true);
    CHECK(view.Focus() == b);
    ++tests;
    CHECK(view.Add(Kind::Label, a, "bad") == NoWidget);
    CHECK(!view.SetLayout(a, 4097, 1));
    for (unsigned i = 5; i < MaxWidgets; ++i)
    {
        CHECK(view.Add(Kind::Label, 0, "x") != NoWidget);
    }
    CHECK(view.Add(Kind::Label, 0, "full") == NoWidget);
    ++tests;
    return 0;
}

static int Editing()
{
    view.Init();
    unsigned field = view.Add(Kind::TextField, 0, "abcdef");
    view.Layout(80, 100);
    CHECK(view.SetFocus(field));
    view.Handle(Key(0xcb, HACKOS_MOD_SHIFT));
    view.Handle(Key(0xcb, HACKOS_MOD_SHIFT));
    CHECK(view.Get(field)->cursor == 4 && view.Get(field)->anchor == 6);
    view.Handle(Key(HACKOS_PHYSICAL_C, HACKOS_MOD_CTRL), Clip);
    CHECK(clipboardLength == 2 && memcmp(clipboard, "ef", 2) == 0);
    view.Handle(Text('X'));
    CHECK(strcmp(view.Get(field)->text, "abcdX") == 0);
    ++tests;
    view.Handle(Key(HACKOS_PHYSICAL_A, HACKOS_MOD_CTRL));
    clipboardFail = true;
    view.Handle(Key(HACKOS_PHYSICAL_X, HACKOS_MOD_CTRL), Clip);
    CHECK(strcmp(view.Get(field)->text, "abcdX") == 0);
    clipboardFail = false;
    view.Handle(Key(HACKOS_PHYSICAL_X, HACKOS_MOD_CTRL), Clip);
    CHECK(view.Get(field)->length == 0);
    view.Handle(Key(HACKOS_PHYSICAL_V, HACKOS_MOD_CTRL), Clip);
    CHECK(strcmp(view.Get(field)->text, "abcdX") == 0);
    ++tests;
    view.Handle(Key(HACKOS_PHYSICAL_A, HACKOS_MOD_CTRL));
    memcpy(clipboard, "a\nb", 3);
    clipboardLength = 3;
    view.Handle(Key(HACKOS_PHYSICAL_V, HACKOS_MOD_CTRL), Clip);
    CHECK(strcmp(view.Get(field)->text, "abcdX") == 0);
    clipboardLength = 256;
    view.Handle(Key(HACKOS_PHYSICAL_V, HACKOS_MOD_CTRL), Clip);
    CHECK(view.Get(field)->length == 5);
    ++tests;
    char full[256];
    memset(full, 'a', 255);
    full[255] = 0;
    CHECK(view.SetText(field, full));
    view.Handle(Text('b'));
    CHECK(view.Get(field)->length == 255 && view.Get(field)->offset > 0);
    view.Handle(Key(0xc7));
    CHECK(view.Get(field)->cursor == 0 && view.Get(field)->offset == 0);
    view.Handle(Key(0xd3));
    CHECK(view.Get(field)->length == 254);
    view.Handle(Key(0xcf));
    view.Handle(Key(HACKOS_PHYSICAL_BACKSPACE));
    CHECK(view.Get(field)->length == 253);
    const char* labels[] = {"OK"};
    CHECK(view.OpenDialog("Dialog", "Space closes", labels, 1, 0));
    CHECK(view.Handle(Key(0x39)).kind == ActionKind::Dialog);
    CHECK(view.Handle(Text(' ')).kind == ActionKind::None && view.Get(field)->length == 253);
    auto release = Key(0x39);
    release.event.type = HACKOS_WINDOW_KEY_UP;
    view.Handle(release);
    view.Handle(Key(0x39));
    CHECK(view.Handle(Text(' ')).kind == ActionKind::Change && view.Get(field)->length == 254);

    ++tests;
    CHECK(view.SetText(field, "abcdef"));
    view.Layout(200, 100);
    auto r = view.Get(field)->bounds;
    view.Handle(Mouse(r.x + 8, r.y + 10, 1));
    view.Handle(Mouse(r.x + 32, r.y + 10, 1));
    view.Handle(Mouse(r.x + 32, r.y + 10, 0));
    CHECK(view.Get(field)->anchor == 0 && view.Get(field)->cursor == 3);
    ++tests;
    return 0;
}

static int Controls()
{
    view.Init();
    unsigned a = view.Add(Kind::Button, 0, "A"), disabled = view.Add(Kind::Button, 0, "Disabled");
    unsigned check = view.Add(Kind::CheckBox, 0, "Check"), list = view.Add(Kind::List, 0);
    const char* items[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    view.SetItems(list, items, 10);
    view.SetLayout(list, 0, 64);
    view.Layout(200, 250);
    view.SetEnabled(disabled, false);
    view.SetFocus(a);
    view.Handle(Key(HACKOS_PHYSICAL_TAB));
    CHECK(view.Focus() == check);
    view.Handle(Key(HACKOS_PHYSICAL_TAB, HACKOS_MOD_SHIFT));
    CHECK(view.Focus() == a);
    ++tests;
    CHECK(view.Handle(Key(HACKOS_PHYSICAL_ENTER)).kind == ActionKind::Activate);
    CHECK(view.Handle(Key(HACKOS_PHYSICAL_ENTER, 0, HACKOS_KEY_REPEAT)).kind == ActionKind::None);
    auto r = view.Get(a)->bounds;
    view.Handle(Mouse(r.x + 5, r.y + 5, 1));
    view.ResetInput(true);
    CHECK(view.Handle(Mouse(r.x + 5, r.y + 5, 0)).kind == ActionKind::None);
    ++tests;
    view.Handle(Mouse(r.x + 5, r.y + 5, 1));
    CHECK(view.Handle(Mouse(999, 999, 0)).kind == ActionKind::None);
    view.Handle(Mouse(r.x + 5, r.y + 5, 1));
    CHECK(view.Handle(Mouse(r.x + 5, r.y + 5, 0)).kind == ActionKind::Activate);
    ++tests;
    view.SetFocus(check);
    CHECK(view.Handle(Key(0x39)).kind == ActionKind::Change && view.Get(check)->checked);
    ++tests;
    view.SetFocus(list);
    view.Handle(Key(0xcf));
    CHECK(view.Get(list)->selected == 9 && view.Get(list)->scroll == 7);
    view.Handle(Key(0xc9));
    CHECK(view.Get(list)->selected == 6 && view.Get(list)->scroll == 6);
    HackosWindowEvent2 wheel = {};
    wheel.event.type = HACKOS_WINDOW_WHEEL;
    wheel.event.key = (uint32_t)-100;
    wheel.event.x = view.Get(list)->bounds.x + 5;
    wheel.event.y = view.Get(list)->bounds.y + 5;
    view.Handle(wheel);
    CHECK(view.Get(list)->scroll == 0 && view.Get(list)->selected == 6);
    // The scroll thumb must lie inside the content clip, not under its border.
    Canvas canvas;
    auto bounds = view.Get(list)->bounds;
    canvas.Init(buffer + 1, 8, {bounds.x + bounds.width - 8, bounds.y, 8, 32});
    view.Draw(canvas, font);
    CHECK(buffer[1 + 3 * 8 + 2] == DefaultTheme().accent);

    ++tests;
    const char* labels[] = {"Accept", "Cancel"};
    CHECK(view.OpenDialog("Title", "Message", labels, 2, 1));
    CHECK(!view.OpenDialog("Title", "Message", labels, 2, 1));
    CHECK(!view.SetFocus(a));
    HackosWindowEvent2 close = {};
    close.event.type = HACKOS_WINDOW_CLOSE_REQUEST;
    CHECK(view.Handle(close).kind == ActionKind::Close && view.DialogOpen());
    view.Handle(Mouse(r.x + 5, r.y + 5, 1));
    CHECK(view.Handle(Mouse(r.x + 5, r.y + 5, 0)).kind == ActionKind::None);
    view.Handle(Key(HACKOS_PHYSICAL_TAB));
    auto result = view.Handle(Key(HACKOS_PHYSICAL_ENTER));
    CHECK(result.kind == ActionKind::Dialog && result.value == 0 && view.Focus() == list);
    ++tests;
    CHECK(view.OpenDialog("Title", "Message", labels, 2, 1));
    result = view.Handle(Key(HACKOS_PHYSICAL_ESCAPE));
    CHECK(result.value == 1 && !view.DialogOpen());
    ++tests;
    view.OpenDialog("Title", "Message", labels, 2, 1);
    r = view.DialogButton(0);
    view.Handle(Mouse(r.x + 3, r.y + 3, 1));
    view.ResetInput(true);
    CHECK(view.Handle(Mouse(r.x + 3, r.y + 3, 0)).kind == ActionKind::None && view.DialogOpen());
    view.Handle(Mouse(r.x + 3, r.y + 3, 1));
    result = view.Handle(Mouse(r.x + 3, r.y + 3, 0));
    CHECK(result.kind == ActionKind::Dialog && result.value == 0);
    ++tests;
    return 0;
}

static int Stress()
{
    view.Init();
    unsigned field = view.Add(Kind::TextField, 0, "test");
    unsigned list = view.Add(Kind::List, 0);
    const char* items[] = {"one", "two"};
    view.SetItems(list, items, 2);
    Canvas canvas;
    uint32_t random = 42;
    for (unsigned i = 0; i < 5000; ++i)
    {
        random = random * 1664525 + 1013904223;
        view.Layout(random % 800, (random >> 12) % 600);
        view.SetFocus(field);
        view.Handle(Key((random & 1) ? 0xcb : 0xcd, random & 15));
        view.Handle(Mouse((int)(random % 1000) - 200, (int)((random >> 10) % 800) - 100, random & 1));
        for (auto& p : buffer)
        {
            p = 0xdeadbeef;
        }
        canvas.Init(buffer + 1, 32, {(int)(random % 500), (int)((random >> 10) % 400), 32, 32});
        view.Draw(canvas, font);
        CHECK(buffer[0] == 0xdeadbeef && buffer[1025] == 0xdeadbeef);
    }
    ++tests;
    return 0;
}

int PlatformTests();

int main()
{
    if (Drawing() || Layout() || Editing() || Controls() || Stress() || PlatformTests())
    {
        return 1;
    }
    printf("GUI core: %u groups passed (including 5000 layout/input/render stress iterations)\n", tests);
    return 0;
}
