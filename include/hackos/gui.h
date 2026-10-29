#pragma once
#include "hackos/abi.h"

namespace Gui
{
constexpr unsigned MaxWidgets = 64, TextCapacity = 256, MaxItems = 1024;
constexpr unsigned NoWidget = ~0U;

struct Rect
{
    int32_t x, y, width, height;
};

Rect Intersect(Rect a, Rect b);
bool Contains(Rect r, int32_t x, int32_t y);

struct Font
{
    uint8_t glyphs[4096];
};

struct Theme
{
    uint32_t background, panel, field, text, muted, border, accent, selection;
};

Theme DefaultTheme();

// A tile uses global client coordinates; every operation intersects both clips.
class Canvas
{
    uint32_t* pixels;
    unsigned stride;
    Rect tile, clip;

  public:
    void Init(uint32_t* buffer, unsigned pitch, Rect bounds);
    Rect Clip() const;
    void SetClip(Rect bounds);
    void Fill(Rect bounds, uint32_t colour);
    void Border(Rect bounds, uint32_t colour);
    void Text(const Font& font, int32_t x, int32_t y, const char* text, uint32_t colour, unsigned limit = TextCapacity);
    static Rect Measure(const char* text, unsigned limit = TextCapacity);
};

enum class Kind
{
    Column,
    Row,
    Label,
    Button,
    CheckBox,
    TextField,
    List,
    TextArea,
    IconButton
};
enum class ActionKind
{
    None,
    Activate,
    Change,
    Close,
    Dialog,
    Command
};

struct Action
{
    ActionKind kind;
    unsigned widget;
    int value;
};

struct Clipboard
{
    void* context;
    uint64_t (*read)(void*, void*, uint64_t);
    uint64_t (*write)(void*, const void*, uint64_t);
};

// Borrowed by a TextArea widget. No global constructor or dynamic allocation.
class TextDocument
{
  public:
    char text[HACKOS_FILE_LIMIT + 1];
    unsigned length, cursor, anchor, top, left;
    bool modified;

    unsigned Start(unsigned position) const;
    unsigned End(unsigned position) const;
    bool Load(const char* bytes, unsigned size);
    bool Replace(const char* bytes, unsigned size);
    void Reveal(Rect bounds);
    void Pointer(Rect bounds, int x, int y, bool extend);
    void Scroll(int rows);
    bool Handle(const HackosWindowEvent2& event, Clipboard clipboard, Rect bounds);
    void Draw(Canvas& canvas, const Font& font, Rect bounds, Theme theme, bool focus) const;
};

struct Widget
{
    Kind kind;
    unsigned parent;
    Rect bounds, clip;
    unsigned width, height, flex, padding, gap;
    bool visible, enabled, checked, secret;
    char text[TextCapacity];
    unsigned length, cursor, anchor, offset;
    const char* const* items;
    unsigned itemCount, selected, scroll;
    TextDocument* document;
    unsigned icon;
};

// Explicit Init is required, including for static storage. No heap, virtual
// methods, global constructors or kernel pointers are used by this library.
class View
{
    Widget widgets[MaxWidgets];
    uint64_t lastClickTime;
    unsigned lastClickWidget, lastClickItem;
    unsigned count, focused, pressed;
    uint32_t buttons;
    bool dragging, dirty, windowFocused, suppressSpace;
    unsigned width, height;
    Theme theme;
    bool modal;
    char dialogTitle[64], dialogMessage[TextCapacity], dialogLabels[3][32];
    unsigned dialogCount, dialogChoice, dialogCancel, dialogPressed, savedFocus;
    Rect dialogBounds, dialogButtons[3];

    bool Available(unsigned id) const;
    bool Focusable(unsigned id) const;
    void NormalizeFocus();
    void Traverse(bool reverse);
    void Arrange(unsigned id);
    void ArrangeDialog();
    void Reveal(Widget& widget);
    bool Insert(Widget& widget, const char* text, unsigned length);
    Action Activate(unsigned id);
    Action DialogReply(unsigned choice);
    Action Edit(unsigned id, const HackosWindowEvent2& event, Clipboard clipboard);

  public:
    void Init();
    unsigned Add(Kind kind, unsigned parent, const char* text = "");
    const Widget* Get(unsigned id) const;
    unsigned Focus() const;
    bool SetFocus(unsigned id);
    bool SetText(unsigned id, const char* text);
    bool SetDocument(unsigned id, TextDocument* document);
    bool SetIcon(unsigned id, unsigned icon);
    bool SetSecret(unsigned id, bool secret);
    bool SetItems(unsigned id, const char* const* items, unsigned length);
    bool SetChecked(unsigned id, bool checked);
    bool SetVisible(unsigned id, bool visible);
    bool SetEnabled(unsigned id, bool enabled);
    bool SetLayout(unsigned id, unsigned width, unsigned height, unsigned flex = 0, unsigned padding = 8,
                   unsigned gap = 8);
    void SetTheme(Theme value);
    void Layout(unsigned width, unsigned height);
    void ResetInput(bool focused);
    bool OpenDialog(const char* title, const char* message, const char* const* labels, unsigned length,
                    unsigned cancel);
    bool DialogOpen() const;
    Rect DialogButton(unsigned choice) const;
    Action Handle(const HackosWindowEvent2& event, Clipboard clipboard = {}, uint64_t milliseconds = 0);
    void Draw(Canvas& canvas, const Font& font) const;
    bool Dirty() const;
    void Invalidate();
    void Painted();
};

// Platform adapter: owns a window, loads the packaged PSF1 font, uploads bounded
// tiles atomically, resynchronizes lost input and blocks in poll when idle.
class Window
{
    uint64_t handle;
    Font font;

  public:
    bool Open(const HackosWindowSpec& spec);
    uint64_t Handle() const;
    bool Paint(View& view);
    uint64_t Next(View& view, Action& action);
    uint64_t Wait(uint64_t timeout = HACKOS_WAIT_FOREVER);
    bool ReplyClose(bool accept);
    void Close();
};
}
