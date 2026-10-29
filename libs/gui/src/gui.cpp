#include "hackos/gui.h"

namespace Gui
{
static int64_t Min(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

static int64_t Max(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static int32_t Clamp(int64_t value, int32_t low, int32_t high)
{
    return (int32_t)Max(low, Min(value, high));
}

static bool Copy(char* destination, unsigned capacity, const char* text, bool multiline = true)
{
    if (!text)
    {
        return false;
    }
    unsigned length = 0;
    while (length < capacity && text[length])
    {
        unsigned ch = (uint8_t)text[length];
        if ((ch < 32 || ch > 126) && !(multiline && ch == '\n'))
        {
            return false;
        }
        ++length;
    }
    if (length == capacity)
    {
        return false;
    }
    for (unsigned i = 0; i <= length; ++i)
    {
        destination[i] = text[i];
    }
    return true;
}

static unsigned Length(const char* text)
{
    unsigned n = 0;
    while (n < TextCapacity && text[n])
    {
        ++n;
    }
    return n;
}

static bool Container(Kind kind)
{
    return kind == Kind::Row || kind == Kind::Column;
}

static unsigned Extent(const Widget& w, bool row)
{
    if (!row)
    {
        return w.height;
    }
    if (w.width)
    {
        return w.width;
    }
    if (Container(w.kind))
    {
        return 0;
    }
    if (w.kind == Kind::TextField || w.kind == Kind::List)
    {
        return 160;
    }
    return Canvas::Measure(w.text).width + (w.kind == Kind::CheckBox ? 40 : 16);
}

static Action Nothing()
{
    return {ActionKind::None, NoWidget, 0};
}

Rect Intersect(Rect a, Rect b)
{
    int64_t x = Max(a.x, b.x), y = Max(a.y, b.y);
    int64_t right = Min((int64_t)a.x + Max(a.width, 0), (int64_t)b.x + Max(b.width, 0));
    int64_t bottom = Min((int64_t)a.y + Max(a.height, 0), (int64_t)b.y + Max(b.height, 0));
    return {(int32_t)x, (int32_t)y, (int32_t)Max(0, right - x), (int32_t)Max(0, bottom - y)};
}

bool Contains(Rect r, int32_t x, int32_t y)
{
    return r.width > 0 && r.height > 0 && x >= r.x && y >= r.y && (int64_t)x < (int64_t)r.x + r.width &&
           (int64_t)y < (int64_t)r.y + r.height;
}

Theme DefaultTheme()
{
    return {0xff17212b, 0xff243545, 0xff101b25, 0xffeeeeee, 0xff8796a5, 0xff526474, 0xff60cfff, 0xff32699a};
}

void Canvas::Init(uint32_t* buffer, unsigned pitch, Rect bounds)
{
    pixels = buffer;
    stride = pitch;
    tile = bounds;
    if (!buffer || bounds.width < 0 || bounds.height < 0 || (unsigned)bounds.width > pitch || pitch > 4096 ||
        bounds.width > 4096 || bounds.height > 2160 || (int64_t)bounds.x + bounds.width > 0x7fffffffLL ||
        (int64_t)bounds.y + bounds.height > 0x7fffffffLL)
    {
        tile.width = tile.height = 0;
    }
    clip = tile;
}

Rect Canvas::Clip() const
{
    return clip;
}

void Canvas::SetClip(Rect bounds)
{
    clip = Intersect(tile, bounds);
}

void Canvas::Fill(Rect bounds, uint32_t colour)
{
    Rect r = Intersect(clip, bounds);
    for (int y = 0; y < r.height; ++y)
    {
        unsigned row = (unsigned)((int64_t)r.y - tile.y + y);
        unsigned column = (unsigned)((int64_t)r.x - tile.x);
        for (int x = 0; x < r.width; ++x)
        {
            pixels[(uint64_t)row * stride + column + x] = colour;
        }
    }
}

void Canvas::Border(Rect r, uint32_t colour)
{
    if (r.width <= 0 || r.height <= 0)
    {
        return;
    }
    Fill({r.x, r.y, r.width, 1}, colour);
    Fill({r.x, r.y, 1, r.height}, colour);
    int64_t right = (int64_t)r.x + r.width - 1, bottom = (int64_t)r.y + r.height - 1;
    if (right <= 0x7fffffff)
    {
        Fill({(int32_t)right, r.y, 1, r.height}, colour);
    }
    if (bottom <= 0x7fffffff)
    {
        Fill({r.x, (int32_t)bottom, r.width, 1}, colour);
    }
}

void Canvas::Text(const Font& font, int32_t left, int32_t top, const char* text, uint32_t colour, unsigned limit)
{
    if (!text)
    {
        return;
    }
    int64_t x = left, y = top;
    for (unsigned n = 0; n < Min(limit, TextCapacity) && text[n]; ++n)
    {
        uint8_t ch = text[n];
        if (ch == '\n')
        {
            x = left;
            y += 16;
            continue;
        }
        if (x <= 0x7fffffff - 8 && y <= 0x7fffffff - 16)
        {
            Rect visible = Intersect(clip, {(int32_t)x, (int32_t)y, 8, 16});
            for (int row = 0; row < visible.height; ++row)
            {
                int py = visible.y + row;
                uint8_t bits = font.glyphs[ch * 16 + (unsigned)(py - y)];
                for (int col = 0; col < visible.width; ++col)
                {
                    int px = visible.x + col;
                    if (bits & (0x80 >> (px - x)))
                    {
                        Fill({px, py, 1, 1}, colour);
                    }
                }
            }
        }
        x += 8;
    }
}

Rect Canvas::Measure(const char* text, unsigned limit)
{
    unsigned x = 0, width = 0, height = 16;
    if (!text)
    {
        return {};
    }
    for (unsigned i = 0; i < Min(limit, TextCapacity) && text[i]; ++i)
    {
        if (text[i] == '\n')
        {
            width = Max(width, x);
            x = 0;
            height += 16;
        }
        else
        {
            x += 8;
        }
    }
    return {0, 0, (int32_t)Max(width, x), (int32_t)height};
}

void View::Init()
{
    count = 1;
    widgets[0] = {};
    widgets[0].kind = Kind::Column;
    widgets[0].parent = NoWidget;
    widgets[0].visible = widgets[0].enabled = true;
    widgets[0].padding = widgets[0].gap = 8;
    focused = pressed = savedFocus = dialogPressed = NoWidget;
    buttons = 0;
    dragging = modal = suppressSpace = false;
    dirty = windowFocused = true;
    width = height = 0;
    theme = DefaultTheme();
}

const Widget* View::Get(unsigned id) const
{
    return id < count ? &widgets[id] : nullptr;
}

unsigned View::Focus() const
{
    return focused;
}

bool View::Available(unsigned id) const
{
    if (id >= count)
    {
        return false;
    }
    for (unsigned node = id; node != NoWidget; node = widgets[node].parent)
    {
        if (!widgets[node].visible || !widgets[node].enabled)
        {
            return false;
        }
    }
    return true;
}

bool View::Focusable(unsigned id) const
{
    return Available(id) && !Container(widgets[id].kind) && widgets[id].kind != Kind::Label;
}

void View::NormalizeFocus()
{
    if (!Focusable(focused))
    {
        focused = NoWidget;
        for (unsigned i = 1; i < count; ++i)
        {
            if (Focusable(i))
            {
                focused = i;
                break;
            }
        }
    }
    if (!Available(pressed))
    {
        pressed = NoWidget;
        dragging = false;
    }
}

bool View::SetFocus(unsigned id)
{
    if (modal || !Focusable(id))
    {
        return false;
    }
    focused = id;
    dirty = true;
    return true;
}

void View::Traverse(bool reverse)
{
    unsigned start = focused < count ? focused : (reverse ? 0 : count - 1);
    for (unsigned n = 1; n <= count; ++n)
    {
        unsigned next = reverse ? (start + count - n) % count : (start + n) % count;
        if (Focusable(next))
        {
            focused = next;
            dirty = true;
            return;
        }
    }
}

unsigned View::Add(Kind kind, unsigned parent, const char* text)
{
    if (count == MaxWidgets || parent >= count || !Container(widgets[parent].kind) ||
        (unsigned)kind > (unsigned)Kind::IconButton)
    {
        return NoWidget;
    }
    Widget value = {};
    if (!Copy(value.text, TextCapacity, text, kind != Kind::TextField))
    {
        return NoWidget;
    }
    value.kind = kind;
    value.parent = parent;
    value.visible = value.enabled = true;
    value.length = Length(value.text);
    value.cursor = value.anchor = value.length;
    value.padding = value.gap = 8;
    value.height = kind == Kind::Label ? Canvas::Measure(text).height : kind == Kind::List ? 120 : 32;
    if (Container(kind))
    {
        value.height = 0;
        value.flex = 1;
    }
    unsigned id = count++;
    widgets[id] = value;
    Layout(width, height);
    return id;
}

bool View::SetSecret(unsigned id, bool secret)
{
    if (id >= count || widgets[id].kind != Kind::TextField)
    {
        return false;
    }
    widgets[id].secret = secret;
    dirty = true;
    return true;
}

bool View::SetText(unsigned id, const char* text)
{
    if (id >= count || !Copy(widgets[id].text, TextCapacity, text, widgets[id].kind != Kind::TextField))
    {
        return false;
    }
    auto& w = widgets[id];
    w.length = Length(w.text);
    if (w.secret)
    {
        volatile char* bytes = w.text;
        for (unsigned i = w.length; i < TextCapacity; ++i)
        {
            bytes[i] = 0;
        }
    }
    w.cursor = w.anchor = w.length;
    w.offset = 0;
    Reveal(w);
    dirty = true;
    return true;
}

bool View::SetItems(unsigned id, const char* const* items, unsigned length)
{
    if (id >= count || widgets[id].kind != Kind::List || length > MaxItems || (length && !items))
    {
        return false;
    }
    for (unsigned i = 0; i < length; ++i)
    {
        if (!items[i])
        {
            return false;
        }
    }
    auto& w = widgets[id];
    w.items = items;
    w.itemCount = length;
    w.selected = w.scroll = 0;
    dirty = true;
    return true;
}

bool View::SetDocument(unsigned id, TextDocument* document)
{
    if (id >= count || widgets[id].kind != Kind::TextArea || !document)
    {
        return false;
    }
    widgets[id].document = document;
    dirty = true;
    return true;
}

bool View::SetIcon(unsigned id, unsigned icon)
{
    if (id >= count || widgets[id].kind != Kind::IconButton || icon < 1 || icon > 2)
    {
        return false;
    }
    widgets[id].icon = icon;
    dirty = true;
    return true;
}

bool View::SetChecked(unsigned id, bool checked)
{
    if (id >= count || widgets[id].kind != Kind::CheckBox)
    {
        return false;
    }
    widgets[id].checked = checked;
    dirty = true;
    return true;
}

bool View::SetVisible(unsigned id, bool visible)
{
    if (id >= count || !id)
    {
        return false;
    }
    widgets[id].visible = visible;
    Layout(width, height);
    return true;
}

bool View::SetEnabled(unsigned id, bool enabled)
{
    if (id >= count)
    {
        return false;
    }
    widgets[id].enabled = enabled;
    NormalizeFocus();
    dirty = true;
    return true;
}

bool View::SetLayout(unsigned id, unsigned w, unsigned h, unsigned flex, unsigned padding, unsigned gap)
{
    if (id >= count || w > 4096 || h > 2160 || flex > 64 || padding > 64 || gap > 64)
    {
        return false;
    }
    auto& node = widgets[id];
    node.width = w;
    node.height = h;
    node.flex = flex;
    node.padding = padding;
    node.gap = gap;
    Layout(width, height);
    return true;
}

void View::SetTheme(Theme value)
{
    theme = value;
    dirty = true;
}

void View::Arrange(unsigned id)
{
    auto& parent = widgets[id];
    if (!Container(parent.kind))
    {
        Reveal(parent);
        return;
    }
    Rect area = parent.bounds;
    unsigned pad = Min(parent.padding, Min(area.width, area.height) / 2);
    area.x += pad;
    area.y += pad;
    area.width -= 2 * pad;
    area.height -= 2 * pad;
    unsigned children = 0, fixed = 0, weights = 0;
    bool row = parent.kind == Kind::Row;
    for (unsigned i = id + 1; i < count; ++i)
    {
        auto& w = widgets[i];
        if (w.parent != id || !w.visible)
        {
            continue;
        }
        ++children;
        if (w.flex)
        {
            weights += w.flex;
        }
        else
        {
            fixed += Extent(w, row);
        }
    }
    int remaining =
        Max(0, (row ? area.width : area.height) - (int64_t)fixed - (int64_t)(children ? children - 1 : 0) * parent.gap);
    int position = row ? area.x : area.y;
    for (unsigned i = id + 1; i < count; ++i)
    {
        auto& w = widgets[i];
        if (w.parent != id || !w.visible)
        {
            continue;
        }
        int extent = Extent(w, row);
        if (w.flex)
        {
            extent = (int64_t)remaining * w.flex / weights;
            remaining -= extent;
            weights -= w.flex;
        }
        // Oversized fixed children consume only the space still inside their parent.
        extent = Min(extent, Max(0, (row ? area.x + area.width : area.y + area.height) - position));
        int cross = row ? (w.height ? Min(w.height, area.height) : area.height)
                        : (w.width ? Min(w.width, area.width) : area.width);
        w.bounds = row ? Rect{position, area.y, extent, cross} : Rect{area.x, position, cross, extent};
        w.clip = Intersect(parent.clip, Intersect(area, w.bounds));
        position += extent + parent.gap;
        Arrange(i);
    }
}

void View::Layout(unsigned w, unsigned h)
{
    bool resized = width != Min(w, 4096) || height != Min(h, 2160);
    width = Min(w, 4096);
    height = Min(h, 2160);
    widgets[0].bounds = widgets[0].clip = {0, 0, (int32_t)width, (int32_t)height};
    Arrange(0);
    if (resized)
    {
        for (unsigned i = 0; i < count; ++i)
        {
            if (widgets[i].kind == Kind::TextArea && widgets[i].document)
            {
                widgets[i].document->Reveal(widgets[i].bounds);
            }
        }
    }
    ArrangeDialog();
    NormalizeFocus();
    dirty = true;
}

void View::Reveal(Widget& w)
{
    if (w.kind == Kind::TextField)
    {
        unsigned columns = Max(1, (w.bounds.width - 16) / 8);
        if (w.cursor < w.offset)
        {
            w.offset = w.cursor;
        }
        if (w.cursor >= w.offset + columns)
        {
            w.offset = w.cursor - columns + 1;
        }
        w.offset = Min(w.offset, w.length >= columns ? w.length - columns + 1 : 0);
    }
    if (w.kind == Kind::List)
    {
        unsigned rows = Max(1, (w.bounds.height - 4) / 20);
        w.scroll = Min(w.scroll, w.itemCount > rows ? w.itemCount - rows : 0);
    }
}

void View::ResetInput(bool value)
{
    windowFocused = value;
    pressed = dialogPressed = NoWidget;
    dragging = suppressSpace = false;
    buttons = 0;
    dirty = true;
}

bool View::Insert(Widget& w, const char* text, unsigned length)
{
    unsigned first = Min(w.cursor, w.anchor), last = Max(w.cursor, w.anchor);
    if (length > TextCapacity - 1 - (w.length - (last - first)))
    {
        return false;
    }
    for (unsigned i = 0; i < length; ++i)
    {
        if ((uint8_t)text[i] < 32 || (uint8_t)text[i] > 126)
        {
            return false;
        }
    }
    unsigned suffix = w.length - last;
    if (first + length > last)
    {
        for (unsigned i = suffix + 1; i; --i)
        {
            w.text[first + length + i - 1] = w.text[last + i - 1];
        }
    }
    else
    {
        for (unsigned i = 0; i <= suffix; ++i)
        {
            w.text[first + length + i] = w.text[last + i];
        }
    }
    for (unsigned i = 0; i < length; ++i)
    {
        w.text[first + i] = text[i];
    }
    w.length = first + length + suffix;
    if (w.secret)
    {
        volatile char* bytes = w.text;
        for (unsigned i = w.length; i < TextCapacity; ++i)
        {
            bytes[i] = 0;
        }
    }
    w.cursor = w.anchor = first + length;
    Reveal(w);
    dirty = true;
    return true;
}

Action View::Edit(unsigned id, const HackosWindowEvent2& input, Clipboard clipboard)
{
    auto& w = widgets[id];
    const auto& event = input.event;
    bool change = false;
    if (w.secret)
    {
        clipboard = {};
    }
    bool control = input.modifiers & HACKOS_MOD_CTRL, shift = input.modifiers & HACKOS_MOD_SHIFT;
    if (event.type == HACKOS_WINDOW_TEXT)
    {
        char text = event.key;
        change = event.key >= 32 && event.key <= 126 && Insert(w, &text, 1);
    }
    else if (event.type == HACKOS_WINDOW_KEY_DOWN)
    {
        unsigned key = event.key;
        if (control && key == HACKOS_PHYSICAL_A)
        {
            w.anchor = 0;
            w.cursor = w.length;
        }
        else if (control && (key == HACKOS_PHYSICAL_C || key == HACKOS_PHYSICAL_X))
        {
            unsigned first = Min(w.cursor, w.anchor), length = Max(w.cursor, w.anchor) - first;
            if (length && clipboard.write && clipboard.write(clipboard.context, w.text + first, length) == length &&
                key == HACKOS_PHYSICAL_X)
            {
                change = Insert(w, "", 0);
            }
        }
        else if (control && key == HACKOS_PHYSICAL_V && clipboard.read)
        {
            char text[TextCapacity];
            uint64_t length = clipboard.read(clipboard.context, text, sizeof(text) - 1);
            if (length <= sizeof(text) - 1)
            {
                change = Insert(w, text, length);
            }
        }
        else if (key == 0xcb || key == 0xcd || key == 0xc7 || key == 0xcf)
        {
            if (key == 0xc7)
            {
                w.cursor = 0;
            }
            else if (key == 0xcf)
            {
                w.cursor = w.length;
            }
            else if (!shift && w.cursor != w.anchor)
            {
                w.cursor = key == 0xcb ? Min(w.cursor, w.anchor) : Max(w.cursor, w.anchor);
            }
            else
            {
                w.cursor = Clamp((int)w.cursor + (key == 0xcb ? -1 : 1), 0, w.length);
            }
            if (!shift)
            {
                w.anchor = w.cursor;
            }
        }
        else if (key == HACKOS_PHYSICAL_BACKSPACE || key == 0xd3)
        {
            if (w.cursor == w.anchor)
            {
                if (key == HACKOS_PHYSICAL_BACKSPACE && w.cursor)
                {
                    --w.anchor;
                }
                else if (key == 0xd3 && w.cursor < w.length)
                {
                    ++w.anchor;
                }
            }
            if (w.cursor != w.anchor)
            {
                change = Insert(w, "", 0);
            }
        }
    }
    Reveal(w);
    dirty = true;
    return change ? Action{ActionKind::Change, id, 0} : Nothing();
}

Action View::Activate(unsigned id)
{
    auto& w = widgets[id];
    if (w.kind == Kind::CheckBox)
    {
        w.checked = !w.checked;
        dirty = true;
        return {ActionKind::Change, id, w.checked};
    }
    if ((w.kind == Kind::Button || w.kind == Kind::IconButton))
    {
        return {ActionKind::Activate, id, 0};
    }
    return Nothing();
}

void View::ArrangeDialog()
{
    int w = Min(width, 520), h = Min(height, 184);
    dialogBounds = {(int)(width - w) / 2, (int)(height - h) / 2, w, h};
    unsigned n = modal ? dialogCount : 0;
    int available = Max(0, w - 24 - (int)(n ? n - 1 : 0) * 8);
    for (unsigned i = 0; i < n; ++i)
    {
        int left = available * i / n, right = available * (i + 1) / n;
        dialogButtons[i] = {dialogBounds.x + 12 + left + (int)i * 8, dialogBounds.y + h - 44, right - left, 32};
    }
}

bool View::OpenDialog(const char* title, const char* message, const char* const* labels, unsigned length,
                      unsigned cancel)
{
    if (modal || !labels || !length || length > 3 || cancel >= length)
    {
        return false;
    }
    char newTitle[64], newMessage[TextCapacity], newLabels[3][32];
    if (!Copy(newTitle, sizeof(newTitle), title) || !Copy(newMessage, sizeof(newMessage), message))
    {
        return false;
    }
    for (unsigned i = 0; i < length; ++i)
    {
        if (!Copy(newLabels[i], 32, labels[i], false))
        {
            return false;
        }
    }
    Copy(dialogTitle, 64, newTitle);
    Copy(dialogMessage, TextCapacity, newMessage);
    for (unsigned i = 0; i < length; ++i)
    {
        Copy(dialogLabels[i], 32, newLabels[i]);
    }
    savedFocus = focused;
    dialogCount = length;
    dialogChoice = dialogCancel = cancel;
    modal = true;
    ResetInput(windowFocused);
    ArrangeDialog();
    return true;
}

bool View::DialogOpen() const
{
    return modal;
}

Rect View::DialogButton(unsigned choice) const
{
    return modal && choice < dialogCount ? dialogButtons[choice] : Rect{};
}

Action View::DialogReply(unsigned choice)
{
    modal = false;
    focused = savedFocus;
    NormalizeFocus();
    ResetInput(windowFocused);
    return {ActionKind::Dialog, NoWidget, (int)choice};
}

Action View::Handle(const HackosWindowEvent2& input, Clipboard clipboard)
{
    const auto& event = input.event;

    // A Space activation also has a queued TEXT event. Do not let that event
    // edit a newly restored/focused field after a dialog or button action.
    if (event.type == HACKOS_WINDOW_KEY_UP && event.key == 0x39)
    {
        suppressSpace = false;
    }
    if (event.type == HACKOS_WINDOW_TEXT && event.key == ' ' && suppressSpace)
    {
        return Nothing();
    }
    if (event.type == HACKOS_WINDOW_INPUT_RESET || event.type == HACKOS_WINDOW_OVERFLOW ||
        event.type == HACKOS_WINDOW_FOCUS)
    {
        ResetInput(event.focused);
        return Nothing();
    }
    if (event.type == HACKOS_WINDOW_RESIZE)
    {
        Layout(event.width, event.height);
        return Nothing();
    }
    if (event.type == HACKOS_WINDOW_CLOSE_REQUEST)
    {
        return {ActionKind::Close, NoWidget, 0};
    }
    if (modal)
    {
        if (event.type == HACKOS_WINDOW_KEY_DOWN)
        {
            dirty = true;
            if (event.key == HACKOS_PHYSICAL_TAB)
            {
                dialogChoice =
                    (dialogChoice + ((input.modifiers & HACKOS_MOD_SHIFT) ? dialogCount - 1 : 1)) % dialogCount;
            }
            else if (!(input.flags & HACKOS_KEY_REPEAT))
            {
                if (event.key == HACKOS_PHYSICAL_ESCAPE)
                {
                    return DialogReply(dialogCancel);
                }
                if (event.key == HACKOS_PHYSICAL_ENTER || event.key == 0x39)
                {
                    auto action = DialogReply(dialogChoice);
                    suppressSpace = event.key == 0x39;
                    return action;
                }
            }
        }
        if (event.type == HACKOS_WINDOW_MOUSE)
        {
            unsigned hit = NoWidget;
            for (unsigned i = 0; i < dialogCount; ++i)
            {
                if (Contains(Intersect(dialogButtons[i], dialogBounds), event.x, event.y))
                {
                    hit = i;
                }
            }
            bool down = (event.buttons & 1) && !(buttons & 1), up = !(event.buttons & 1) && (buttons & 1);
            buttons = event.buttons;
            if (down)
            {
                dialogPressed = hit;
                if (hit != NoWidget)
                {
                    dialogChoice = hit;
                }
                dirty = true;
            }
            if (up)
            {
                unsigned old = dialogPressed;
                dialogPressed = NoWidget;
                dirty = true;
                if (hit != NoWidget && hit == old)
                {
                    return DialogReply(hit);
                }
            }
        }
        return Nothing();
    }
    NormalizeFocus();
    if (event.type == HACKOS_WINDOW_KEY_DOWN && (input.modifiers & HACKOS_MOD_CTRL) &&
        !(input.flags & HACKOS_KEY_REPEAT) && (event.key == 0x1f || event.key == 0x18 || event.key == 0x31))
    {
        return {ActionKind::Command, focused, (int)event.key};
    }
    if (event.type == HACKOS_WINDOW_KEY_DOWN && event.key == HACKOS_PHYSICAL_TAB)
    {
        Traverse(input.modifiers & HACKOS_MOD_SHIFT);
        return Nothing();
    }
    if (event.type == HACKOS_WINDOW_MOUSE || event.type == HACKOS_WINDOW_WHEEL)
    {
        unsigned hit = NoWidget;
        for (unsigned i = 1; i < count; ++i)
        {
            if (Focusable(i) && Contains(widgets[i].clip, event.x, event.y))
            {
                hit = i;
            }
        }
        if (event.type == HACKOS_WINDOW_WHEEL)
        {
            if (hit != NoWidget && widgets[hit].kind == Kind::TextArea && widgets[hit].document)
            {
                widgets[hit].document->Scroll((int32_t)event.key);
                dirty = true;
            }
            if (hit != NoWidget && widgets[hit].kind == Kind::List)
            {
                auto& w = widgets[hit];
                w.scroll = Clamp((int64_t)w.scroll + (int32_t)event.key, 0, w.itemCount);
                Reveal(w);
                dirty = true;
            }
            return Nothing();
        }
        bool down = (event.buttons & 1) && !(buttons & 1), up = !(event.buttons & 1) && (buttons & 1);
        buttons = event.buttons;
        if (down)
        {
            pressed = hit;
            dragging = false;
            if (hit != NoWidget)
            {
                focused = hit;
                dirty = true;
            }
        }
        if (pressed != NoWidget && widgets[pressed].kind == Kind::TextArea && widgets[pressed].document &&
            (down || (event.buttons & 1)))
        {
            auto& w = widgets[pressed];
            w.document->Pointer(w.bounds, event.x, event.y, !down || (input.modifiers & HACKOS_MOD_SHIFT));
            dragging = true;
            dirty = true;
        }
        if (pressed != NoWidget && widgets[pressed].kind == Kind::TextField && (down || (event.buttons & 1)))
        {
            auto& w = widgets[pressed];
            w.cursor = Clamp((int64_t)w.offset + ((int64_t)event.x - w.bounds.x - 8) / 8, 0, w.length);
            if (down && !(input.modifiers & HACKOS_MOD_SHIFT))
            {
                w.anchor = w.cursor;
            }
            dragging = true;
            Reveal(w);
            dirty = true;
        }
        if (down && hit != NoWidget && widgets[hit].kind == Kind::List && widgets[hit].itemCount)
        {
            auto& w = widgets[hit];
            unsigned selected = w.scroll + Max(0, (event.y - w.bounds.y - 2) / 20);
            if (selected < w.itemCount)
            {
                w.selected = selected;
                return {ActionKind::Change, hit, (int)selected};
            }
        }
        if (up)
        {
            unsigned old = pressed;
            pressed = NoWidget;
            dragging = false;
            dirty = true;
            if (old != NoWidget && old == hit)
            {
                return Activate(hit);
            }
        }
        return Nothing();
    }
    if (!Focusable(focused))
    {
        return Nothing();
    }
    auto& w = widgets[focused];
    if (w.kind == Kind::TextArea && w.document)
    {
        bool changed = w.document->Handle(input, clipboard, w.bounds);
        dirty = true;
        return changed ? Action{ActionKind::Change, focused, 0} : Nothing();
    }
    if (w.kind == Kind::TextField)
    {
        return Edit(focused, input, clipboard);
    }
    if (event.type == HACKOS_WINDOW_KEY_DOWN)
    {
        if (!(input.flags & HACKOS_KEY_REPEAT) && (event.key == HACKOS_PHYSICAL_ENTER || event.key == 0x39))
        {
            suppressSpace = event.key == 0x39;
            return Activate(focused);
        }
        if (w.kind == Kind::List && w.itemCount)
        {
            int next = w.selected;
            int rows = Max(1, (w.bounds.height - 4) / 20);
            if (event.key == 0xc8)
            {
                --next;
            }
            else if (event.key == 0xd0)
            {
                ++next;
            }
            else if (event.key == 0xc7)
            {
                next = 0;
            }
            else if (event.key == 0xcf)
            {
                next = w.itemCount - 1;
            }
            else if (event.key == 0xc9)
            {
                next -= rows;
            }
            else if (event.key == 0xd1)
            {
                next += rows;
            }
            else
            {
                return Nothing();
            }
            w.selected = Clamp(next, 0, w.itemCount - 1);
            if (w.selected < w.scroll)
            {
                w.scroll = w.selected;
            }
            if (w.selected >= w.scroll + rows)
            {
                w.scroll = w.selected - rows + 1;
            }
            dirty = true;
            return {ActionKind::Change, focused, (int)w.selected};
        }
    }
    return Nothing();
}

void View::Draw(Canvas& canvas, const Font& font) const
{
    Rect original = canvas.Clip();
    canvas.Fill({0, 0, (int)width, (int)height}, theme.background);
    for (unsigned i = 1; i < count; ++i)
    {
        const auto& w = widgets[i];
        bool visible = true;
        for (unsigned p = i; p != NoWidget; p = widgets[p].parent)
        {
            visible &= widgets[p].visible;
        }
        if (!visible)
        {
            continue;
        }
        canvas.SetClip(Intersect(original, w.clip));
        bool active = Available(i);
        uint32_t ink = active ? theme.text : theme.muted;
        bool focus = i == focused && windowFocused && !modal;
        if (Container(w.kind))
        {
            canvas.Fill(w.bounds, theme.panel);
        }
        else if (w.kind == Kind::Label)
        {
            canvas.Text(font, w.bounds.x, w.bounds.y, w.text, ink);
        }
        else
        {
            canvas.Fill(w.bounds, pressed == i && !dragging ? theme.selection : theme.field);
            canvas.Border(w.bounds, focus ? theme.accent : theme.border);
            Rect inner = {w.bounds.x + 4, w.bounds.y + 2, (int)Max(0, w.bounds.width - 8),
                          (int)Max(0, w.bounds.height - 4)};
            canvas.SetClip(Intersect(original, Intersect(w.clip, inner)));
            if (w.kind == Kind::TextField)
            {
                int left = w.bounds.x + 8, top = w.bounds.y + 8;
                unsigned first = Min(w.cursor, w.anchor), last = Max(w.cursor, w.anchor);
                canvas.Fill({left + ((int)first - (int)w.offset) * 8, top, (int)(last - first) * 8, 16},
                            theme.selection);
                if (w.secret)
                {
                    for (unsigned j = w.offset; j < w.length; ++j)
                    {
                        canvas.Text(font, left + (j - w.offset) * 8, top, "*", ink);
                    }
                }
                else
                {
                    canvas.Text(font, left, top, w.text + w.offset, ink);
                }
                if (focus)
                {
                    canvas.Fill({left + ((int)w.cursor - (int)w.offset) * 8, top, 1, 16}, theme.accent);
                }
            }
            else if (w.kind == Kind::TextArea && w.document)
            {
                w.document->Draw(canvas, font, w.bounds, theme, focus);
            }
            else if (w.kind == Kind::IconButton)
            {
                int x = w.bounds.x + (w.bounds.width - 48) / 2, y = w.bounds.y + 10;
                if (w.icon == 1)
                {
                    canvas.Fill({x, y, 48, 36}, theme.border);
                    canvas.Fill({x + 3, y + 3, 42, 30}, theme.background);
                    canvas.Text(font, x + 8, y + 10, ">_", theme.accent);
                    canvas.Fill({x + 18, y + 36, 12, 4}, theme.border);
                    canvas.Fill({x + 10, y + 40, 28, 3}, theme.border);
                }
                else
                {
                    canvas.Fill({x + 2, y + 4, 20, 10}, 0xffd6a84b);
                    canvas.Fill({x, y + 12, 48, 30}, 0xffe9bd62);
                    canvas.Fill({x + 3, y + 16, 42, 3}, 0xffffd982);
                }
                canvas.Text(font, w.bounds.x + (w.bounds.width - (int)Canvas::Measure(w.text).width) / 2, y + 50,
                            w.text, ink);
            }
            else if (w.kind == Kind::List)
            {
                unsigned rows = Max(0, (w.bounds.height - 4) / 20);
                for (unsigned row = 0; row < rows && w.scroll + row < w.itemCount; ++row)
                {
                    int y = w.bounds.y + 2 + row * 20;
                    if (w.selected == w.scroll + row)
                    {
                        canvas.Fill({w.bounds.x + 2, y, w.bounds.width - 4, 20}, theme.selection);
                    }
                    canvas.Text(font, w.bounds.x + 8, y + 2, w.items[w.scroll + row], ink);
                }
                if (w.itemCount > rows && rows)
                {
                    int track = w.bounds.height - 4, thumb = Max(4, (int64_t)track * rows / w.itemCount);
                    int y = w.bounds.y + 2 + (int64_t)(track - thumb) * w.scroll / (w.itemCount - rows);
                    canvas.Fill({w.bounds.x + w.bounds.width - 6, y, 2, thumb}, theme.accent);
                }
            }
            else if (w.kind == Kind::CheckBox)
            {
                Rect box = {w.bounds.x + 8, w.bounds.y + 8, 16, 16};
                canvas.Border(box, ink);
                if (w.checked)
                {
                    canvas.Fill({box.x + 4, box.y + 4, 8, 8}, theme.accent);
                }
                canvas.Text(font, w.bounds.x + 32, w.bounds.y + 8, w.text, ink);
            }
            else
            {
                canvas.Text(font, w.bounds.x + 8, w.bounds.y + 8, w.text, ink);
            }
        }
    }
    if (modal)
    {
        canvas.SetClip(Intersect(original, dialogBounds));
        canvas.Fill(dialogBounds, theme.panel);
        canvas.Border(dialogBounds, theme.accent);
        canvas.Text(font, dialogBounds.x + 12, dialogBounds.y + 12, dialogTitle, theme.text, 64);
        canvas.SetClip(
            Intersect(original, {dialogBounds.x + 12, dialogBounds.y + 36, (int)Max(0, dialogBounds.width - 24),
                                 (int)Max(0, dialogBounds.height - 88)}));
        canvas.Text(font, dialogBounds.x + 12, dialogBounds.y + 40, dialogMessage, theme.text);
        for (unsigned i = 0; i < dialogCount; ++i)
        {
            canvas.SetClip(Intersect(original, Intersect(dialogBounds, dialogButtons[i])));
            canvas.Fill(dialogButtons[i], i == dialogChoice ? theme.selection : theme.field);
            canvas.Border(dialogButtons[i], i == dialogChoice ? theme.accent : theme.border);
            canvas.Text(font, dialogButtons[i].x + 8, dialogButtons[i].y + 8, dialogLabels[i], theme.text, 32);
        }
    }
    canvas.SetClip(original);
}

bool View::Dirty() const
{
    return dirty;
}

void View::Invalidate()
{
    dirty = true;
}

void View::Painted()
{
    dirty = false;
}
}
