#pragma once
#include "hackos/user.h"

// A bounded interaction diagnostic, not the widget toolkit planned for step 3.
class InteractionDemo
{
    char fields[3][64];
    unsigned lengths[3], cursors[3], focus, choice;
    bool selected, dirty, confirming;
    uint8_t font[4096];
    char clipboard[HACKOS_CLIPBOARD_LIMIT];
    const char* status;

    bool TextPixel(const char* text, unsigned left, unsigned top, unsigned x, unsigned y) const
    {
        if (x < left || y < top || y >= top + 16)
        {
            return false;
        }
        unsigned index = (x - left) / 8;
        for (unsigned i = 0; i <= index; ++i)
        {
            if (!text[i])
            {
                return false;
            }
        }
        return font[(uint8_t)text[index] * 16 + y - top] & (0x80 >> ((x - left) % 8));
    }

    void ClearSelection()
    {
        if (selected)
        {
            lengths[focus] = cursors[focus] = 0;
            fields[focus][0] = 0;
            selected = false;
        }
    }

    void Insert(const char* text, unsigned length)
    {
        unsigned retained = selected ? 0 : lengths[focus];
        if (length > 63 - retained)
        {
            status = "Paste does not fit; field unchanged.";
            return;
        }
        for (unsigned i = 0; i < length; ++i)
        {
            if ((uint8_t)text[i] < 32 || (uint8_t)text[i] > 126)
            {
                status = "This demo accepts printable ASCII only.";
                return;
            }
        }
        ClearSelection();
        auto& size = lengths[focus];
        auto& cursor = cursors[focus];
        for (unsigned i = size + 1; i > cursor; --i)
        {
            fields[focus][i - 1 + length] = fields[focus][i - 1];
        }
        for (unsigned i = 0; i < length; ++i)
        {
            fields[focus][cursor + i] = text[i];
        }
        cursor += length;
        size += length;
        dirty = true;
        status = "Unsaved changes. Ctrl-S saves /tmp/gui-notes.txt.";
    }

    bool Save()
    {
        char data[192];
        unsigned length = 0;
        for (unsigned row = 0; row < 3; ++row)
        {
            for (unsigned i = 0; i < lengths[row]; ++i)
            {
                data[length++] = fields[row][i];
            }
            data[length++] = '\n';
        }
        HackosFileInfo info;
        uint64_t found = syscall_stat("/tmp/gui-notes.txt", &info);
        if ((found != 0 && found != HACKOS_NOT_FOUND) ||
            syscall_file_save("/tmp/gui-notes.txt", data, length, found == HACKOS_NOT_FOUND ? HACKOS_SAVE_CREATE : 0) !=
                0)
        {
            status = "Save failed; changes are still here.";
            User::Print("GUI_SAVE_FAILED\n");
            return false;
        }
        dirty = false;
        status = "Saved /tmp/gui-notes.txt.";
        User::Print("GUI_SAVED\n");
        return true;
    }

    bool Reply(uint64_t window, unsigned action)
    {
        if (action == 2)
        {
            syscall_window_close_reply(window, 0);
            confirming = false;
            User::Print("GUI_CANCEL\n");
            return true;
        }
        if (action == 0 && !Save())
        {
            return true;
        }
        return syscall_window_close_reply(window, 1) != 0;
    }

  public:
    void LoadFont()
    {
        // Static storage is zero-initialized; freestanding startup runs no constructors.
        status = "Three fields. Tab / Shift-Tab changes focus.";
        uint64_t fd = syscall_fd_open("~/UTIL/FONTS/zap-light16.psf", HACKOS_OPEN_READ);
        if (fd == HACKOS_ERROR)
        {
            return;
        }
        uint8_t header[4];
        if (syscall_fd_read(fd, header, 4, 0) == 4 && header[0] == 0x36 && header[1] == 4 && header[3] == 16)
        {
            for (unsigned i = 0; i < sizeof(font); i += 1024)
            {
                if (syscall_fd_read(fd, font + i, 1024, 0) != 1024)
                {
                    break;
                }
            }
        }
        syscall_fd_close(fd);
    }

    uint32_t Pixel(unsigned x, unsigned y) const
    {
        uint32_t colour = 0xff17212b;
        for (unsigned row = 0; row < 3; ++row)
        {
            unsigned top = 90 + row * 44;
            if (x >= 16 && x < 544 && y >= top && y < top + 32)
            {
                bool edge = x < 18 || x >= 542 || y < top + 2 || y >= top + 30;
                colour = edge ? (focus == row ? 0xff60cfff : 0xff526474)
                              : (focus == row && selected ? 0xff32699a : 0xff263544);
                if (TextPixel(fields[row], 24, top + 8, x, y))
                {
                    colour = 0xffeeeeee;
                }
                if (!confirming && focus == row && x == 24 + cursors[row] * 8 && y >= top + 7 && y < top + 25)
                {
                    colour = 0xffffffff;
                }
            }
        }
        if (TextPixel("Keyboard and clipboard", 16, 12, x, y) ||
            TextPixel("Ctrl-A select; Ctrl-C copy; Ctrl-X cut; Ctrl-V paste", 16, 36, x, y) ||
            TextPixel("Ctrl-S save. Alt-F4 requests close.", 16, 56, x, y) || TextPixel(status, 16, 238, x, y))
        {
            colour = 0xffeeeeee;
        }
        if (confirming && x >= 12 && x < 600 && y >= 80 && y < 224)
        {
            colour = 0xff34485c;
            if (TextPixel("Unsaved changes", 28, 96, x, y) ||
                TextPixel("Save and close, discard, or cancel?", 28, 120, x, y))
            {
                colour = 0xffffffff;
            }
            const char* labels[] = {"Save + close", "Discard", "Cancel"};
            for (unsigned i = 0; i < 3; ++i)
            {
                unsigned left = 28 + i * 180;
                if (x >= left && x < left + 164 && y >= 164 && y < 200)
                {
                    colour = choice == i ? 0xff39799b : 0xff23313f;
                    if (TextPixel(labels[i], left + 12, 174, x, y))
                    {
                        colour = 0xffffffff;
                    }
                }
            }
        }
        return colour;
    }

    bool Event(uint64_t window, const HackosWindowEvent2& input, bool& repaint)
    {
        const auto& event = input.event;
        if (event.type == HACKOS_WINDOW_CLOSE_REQUEST)
        {
            if (!dirty)
            {
                return syscall_window_close_reply(window, 1) != 0;
            }
            confirming = true;
            User::Print("GUI_CONFIRM\n");
            choice = 2;
            repaint = true;
        }
        else if (event.type == HACKOS_WINDOW_INPUT_RESET || event.type == HACKOS_WINDOW_OVERFLOW)
        {
            // No action is inferred from lost/replayed input; resynchronize first.
            HackosWindowInputState state;
            syscall_window_input(window, HACKOS_INPUT_QUERY, &state);
            repaint = true;
        }
        else if (event.type == HACKOS_WINDOW_MOUSE && (event.buttons & 1))
        {
            repaint = true;
            if (confirming && event.y >= 164 && event.y < 200 && event.x >= 28 && event.x < 552)
            {
                return Reply(window, (event.x - 28) / 180);
            }
            if (!confirming && event.y >= 90 && event.y < 210 && event.x >= 16 && event.x < 544)
            {
                focus = (event.y - 90) / 44;
                cursors[focus] = lengths[focus];
                selected = false;
                repaint = true;
            }
        }
        else if (event.type == HACKOS_WINDOW_KEY_DOWN)
        {
            repaint = true;
            bool control = input.modifiers & HACKOS_MOD_CTRL;
            if (event.key == HACKOS_PHYSICAL_TAB)
            {
                auto& target = confirming ? choice : focus;
                target = (target + ((input.modifiers & HACKOS_MOD_SHIFT) ? 2 : 1)) % 3;
                selected = false;
            }
            else if (confirming)
            {
                if (event.key == HACKOS_PHYSICAL_ESCAPE)
                {
                    return Reply(window, 2);
                }
                if (event.key == HACKOS_PHYSICAL_ENTER)
                {
                    return Reply(window, choice);
                }
            }
            else if (control && event.key == HACKOS_PHYSICAL_A)
            {
                selected = true;
            }
            else if (control && (event.key == HACKOS_PHYSICAL_C || event.key == HACKOS_PHYSICAL_X))
            {
                if (selected &&
                    syscall_clipboard(HACKOS_CLIPBOARD_WRITE, fields[focus], lengths[focus]) == lengths[focus])
                {
                    status = "Copied selection to this session's clipboard.";
                    if (event.key == HACKOS_PHYSICAL_X)
                    {
                        ClearSelection();
                        dirty = true;
                    }
                }
            }
            else if (control && event.key == HACKOS_PHYSICAL_V)
            {
                uint64_t count = syscall_clipboard(HACKOS_CLIPBOARD_READ, clipboard, sizeof(clipboard));
                if (count <= sizeof(clipboard))
                {
                    Insert(clipboard, count);
                }
            }
            else if (control && event.key == HACKOS_PHYSICAL_S)
            {
                Save();
            }
            else if (event.key == HACKOS_PHYSICAL_BACKSPACE || event.key == 0xd3)
            {
                auto& cursor = cursors[focus];
                auto& length = lengths[focus];
                if (selected)
                {
                    ClearSelection();
                    dirty = true;
                }
                else if ((event.key == HACKOS_PHYSICAL_BACKSPACE && cursor) || (event.key == 0xd3 && cursor < length))
                {
                    if (event.key == HACKOS_PHYSICAL_BACKSPACE)
                    {
                        --cursor;
                    }
                    for (unsigned i = cursor; i < length; ++i)
                    {
                        fields[focus][i] = fields[focus][i + 1];
                    }
                    --length;
                    dirty = true;
                }
            }
            else if (event.key == 0xcb || event.key == 0xcd)
            {
                selected = false;
                if (event.key == 0xcb && cursors[focus])
                {
                    --cursors[focus];
                }
                if (event.key == 0xcd && cursors[focus] < lengths[focus])
                {
                    ++cursors[focus];
                }
            }
        }
        else if (event.type == HACKOS_WINDOW_TEXT && !confirming)
        {
            char ch = (char)event.key;
            Insert(&ch, 1);
            repaint = true;
        }
        return true;
    }
};
