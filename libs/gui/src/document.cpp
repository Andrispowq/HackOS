#include "hackos/gui.h"

namespace Gui
{
static unsigned Min(unsigned a, unsigned b)
{
    return a < b ? a : b;
}

static unsigned Max(unsigned a, unsigned b)
{
    return a > b ? a : b;
}

static bool Valid(const char* text, unsigned length)
{
    for (unsigned i = 0; i < length; ++i)
    {
        unsigned char c = text[i];
        if ((c < 32 || c > 126) && c != '\n' && c != '\t')
        {
            return false;
        }
    }
    return true;
}

unsigned TextDocument::Start(unsigned position) const
{
    while (position && text[position - 1] != '\n')
    {
        --position;
    }
    return position;
}

unsigned TextDocument::End(unsigned position) const
{
    while (position < length && text[position] != '\n')
    {
        ++position;
    }
    return position;
}

bool TextDocument::Load(const char* bytes, unsigned size)
{
    if (size > HACKOS_FILE_LIMIT || (!bytes && size) || !Valid(bytes, size))
    {
        return false;
    }
    for (unsigned i = 0; i < size; ++i)
    {
        text[i] = bytes[i];
    }
    text[size] = 0;
    length = size;
    cursor = anchor = top = left = 0;
    modified = false;
    return true;
}

bool TextDocument::Replace(const char* bytes, unsigned size)
{
    unsigned first = Min(cursor, anchor), last = Max(cursor, anchor), suffix = length - last;
    if ((!bytes && size) || size > HACKOS_FILE_LIMIT - (length - (last - first)) || !Valid(bytes, size))
    {
        return false;
    }
    if (!size && first == last)
    {
        return false;
    }
    if (first + size > last)
    {
        for (unsigned i = suffix + 1; i; --i)
        {
            text[first + size + i - 1] = text[last + i - 1];
        }
    }
    else
    {
        for (unsigned i = 0; i <= suffix; ++i)
        {
            text[first + size + i] = text[last + i];
        }
    }
    for (unsigned i = 0; i < size; ++i)
    {
        text[first + i] = bytes[i];
    }
    length = first + size + suffix;
    cursor = anchor = first + size;
    top = Start(Min(top, length));
    modified = true;
    return true;
}

void TextDocument::Reveal(Rect bounds)
{
    unsigned rows = bounds.height > 8 ? Max(1, (bounds.height - 8) / 16) : 1;
    unsigned columns = bounds.width > 24 ? (bounds.width - 16) / 8 : 1;
    if (cursor < top)
    {
        top = Start(cursor);
    }
    unsigned row = 0;
    for (unsigned i = top; i < cursor; ++i)
    {
        if (text[i] == '\n')
        {
            ++row;
        }
    }
    while (row >= rows)
    {
        top = Min(length, End(top) + 1);
        --row;
    }
    unsigned column = cursor - Start(cursor);
    if (column < left)
    {
        left = column;
    }
    if (column >= left + columns)
    {
        left = column - columns + 1;
    }
}

void TextDocument::Scroll(int rows)
{
    while (rows > 0 && End(top) < length)
    {
        top = End(top) + 1;
        --rows;
    }
    while (rows < 0 && top)
    {
        top = Start(top - 1);
        ++rows;
    }
}

void TextDocument::Pointer(Rect bounds, int x, int y, bool extend)
{
    int row = (y - bounds.y - 4) / 16;
    unsigned position = top;
    if (y < bounds.y)
    {
        Scroll(-1);
        position = top;
    }
    else if (y >= bounds.y + bounds.height)
    {
        Scroll(1);
        position = top;
        row = bounds.height > 8 ? (bounds.height - 8) / 16 - 1 : 0;
    }
    while (row-- > 0 && position < length)
    {
        position = Min(length, End(position) + 1);
    }
    int column = (x - bounds.x - 8) / 8;
    cursor = Min(End(position), position + left + (column > 0 ? column : 0));
    if (!extend)
    {
        anchor = cursor;
    }
}

bool TextDocument::Handle(const HackosWindowEvent2& input, Clipboard clipboard, Rect bounds)
{
    auto& event = input.event;
    bool changed = false;
    bool control = input.modifiers & HACKOS_MOD_CTRL, shift = input.modifiers & HACKOS_MOD_SHIFT;
    if (event.type == HACKOS_WINDOW_TEXT && !control && !(input.modifiers & HACKOS_MOD_ALT))
    {
        char c = event.key;
        changed = event.key >= 32 && event.key <= 126 && Replace(&c, 1);
    }
    else if (event.type == HACKOS_WINDOW_KEY_DOWN)
    {
        unsigned key = event.key;
        if (control && key == HACKOS_PHYSICAL_A)
        {
            anchor = 0;
            cursor = length;
        }
        else if (control && (key == HACKOS_PHYSICAL_C || key == HACKOS_PHYSICAL_X))
        {
            unsigned first = Min(cursor, anchor), size = Max(cursor, anchor) - first;
            if (size && size <= HACKOS_CLIPBOARD_LIMIT && clipboard.write &&
                clipboard.write(clipboard.context, text + first, size) == size && key == HACKOS_PHYSICAL_X)
            {
                changed = Replace("", 0);
            }
        }
        else if (control && key == HACKOS_PHYSICAL_V && clipboard.read)
        {
            char paste[HACKOS_CLIPBOARD_LIMIT];
            auto size = clipboard.read(clipboard.context, paste, sizeof(paste));
            if (size <= sizeof(paste))
            {
                changed = Replace(paste, size);
            }
        }
        else if (!control && key == HACKOS_PHYSICAL_ENTER)
        {
            changed = Replace("\n", 1);
        }
        else if (control && key == 0x17) // Ctrl-I inserts a tab; Tab navigates widgets.
        {
            changed = Replace("\t", 1);
        }
        else if (key == HACKOS_PHYSICAL_BACKSPACE || key == 0xd3)
        {
            if (cursor == anchor)
            {
                if (key == HACKOS_PHYSICAL_BACKSPACE && anchor)
                {
                    --anchor;
                }
                else if (key == 0xd3 && anchor < length)
                {
                    ++anchor;
                }
            }
            changed = Replace("", 0);
        }
        else if (key == 0xcb || key == 0xcd || key == 0xc7 || key == 0xcf || key == 0xc8 || key == 0xd0 ||
                 key == 0xc9 || key == 0xd1)
        {
            if (key == 0xc7)
            {
                cursor = control ? 0 : Start(cursor);
            }
            else if (key == 0xcf)
            {
                cursor = control ? length : End(cursor);
            }
            else if (key == 0xcb || key == 0xcd)
            {
                cursor = !shift && cursor != anchor ? (key == 0xcb ? Min(cursor, anchor) : Max(cursor, anchor))
                         : key == 0xcb              ? (cursor ? cursor - 1 : 0)
                                                    : Min(length, cursor + 1);
            }
            else
            {
                unsigned column = cursor - Start(cursor), position = Start(cursor);
                unsigned steps = key == 0xc9 || key == 0xd1 ? (bounds.height > 24 ? (bounds.height - 8) / 16 : 1) : 1;
                while (steps--)
                {
                    if (key == 0xc8 || key == 0xc9)
                    {
                        position = position ? Start(position - 1) : 0;
                    }
                    else if (End(position) < length)
                    {
                        position = End(position) + 1;
                    }
                }
                cursor = Min(End(position), position + column);
            }
            if (!shift)
            {
                anchor = cursor;
            }
        }
    }
    Reveal(bounds);
    return changed;
}

void TextDocument::Draw(Canvas& canvas, const Font& font, Rect bounds, Theme theme, bool focus) const
{
    unsigned position = top, row = 0;
    unsigned first = Min(cursor, anchor), last = Max(cursor, anchor);
    int y = bounds.y + 4;
    Rect clip = canvas.Clip();
    while (y + (int)row * 16 < bounds.y + bounds.height - 4)
    {
        unsigned end = End(position);
        int topY = y + row * 16;
        if (topY < clip.y + clip.height && topY + 16 > clip.y)
        {
            unsigned visible = Min(end, position + left);
            unsigned limit = bounds.width > 8 ? (bounds.width - 8) / 8 : 0;
            for (unsigned column = 0; column <= limit && visible + column <= end; ++column)
            {
                unsigned index = visible + column;
                int x = bounds.x + 8 + ((int)(index - position) - (int)left) * 8;
                if (index >= first && index < last)
                {
                    canvas.Fill({x, topY, 8, 16}, theme.selection);
                }
                if (index < end)
                {
                    char glyph[2] = {text[index] == '\t' ? ' ' : text[index], 0};
                    canvas.Text(font, x, topY, glyph, theme.text, 1);
                }
                if (focus && index == cursor)
                {
                    canvas.Fill({x, topY, 1, 16}, theme.accent);
                }
            }
        }
        if (end == length)
        {
            break;
        }
        position = end + 1;
        ++row;
    }
}
}
