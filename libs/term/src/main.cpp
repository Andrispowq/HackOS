#include "hackos/gui.h"
#include "hackos/user.h"
#include "hackos/stdio.h"
#include "hackos/runtime/terminal_view.h"
#include "hackos/runtime/terminal_appearance.h"
using namespace User;
static Gui::Window window;
static Gui::Font font;
static TerminalViewport viewport;
static HackosTerminalView snapshot;
static HackosTerminalSettings appearance;
static char copied[HACKOS_CLIPBOARD_LIMIT], notice[80];
static uint32_t pixels[1024];

static unsigned Key(const HackosWindowEvent2& event)
{
    if (event.event.type == HACKOS_WINDOW_TEXT)
    {
        return event.event.key;
    }
    if (event.event.type != HACKOS_WINDOW_KEY_DOWN)
    {
        return 0;
    }
    unsigned key = event.event.key;
    if (event.modifiers & HACKOS_MOD_CTRL)
    {
        if (key == 0x2e)
        {
            return HACKOS_KEY_INTERRUPT;
        }
        if (key == 0x2c)
        {
            return HACKOS_KEY_STOP;
        }
        if (key == 0x20)
        {
            return 4;
        }
        if (key == 0x1f)
        {
            return 19;
        }
        if (key == 0x2d)
        {
            return 24;
        }
    }
    switch (key)
    {
    case 0x1c:
    case 0x9c:
        return '\n';
    case 0x0e:
        return '\b';
    case 0x0f:
        return '\t';
    case 0xc8:
        return HACKOS_KEY_UP;
    case 0xd0:
        return HACKOS_KEY_DOWN;
    case 0xcb:
        return HACKOS_KEY_LEFT;
    case 0xcd:
        return HACKOS_KEY_RIGHT;
    case 0xd3:
        return HACKOS_KEY_DELETE;
    case 0xc7:
        return 133;
    case 0xcf:
        return 134;
    case 0x01:
        return 27;
    default:
        return 0;
    }
}

static bool Paint()
{
    const auto& state = viewport.image.state;
    unsigned height = appearance.fontSize, width = height / 2;
    uint32_t background = TerminalBlend(appearance.bgColour, 0xff000000);
    uint32_t foreground = TerminalBlend(appearance.fontColour, background);
    HackosWindowFrame frame = {};
    if (syscall_window_frame(window.Handle(), HACKOS_WINDOW_BEGIN, &frame))
    {
        return false;
    }
    for (unsigned y = 0; y < frame.height; y += 1)
    {
        for (unsigned x = 0; x < frame.width; x += 1024)
        {
            unsigned w = frame.width - x < 1024 ? frame.width - x : 1024;
            Gui::Canvas canvas;
            canvas.Init(pixels, w, {(int)x, (int)y, (int)w, 1});
            canvas.Fill({0, 0, (int)frame.width, (int)frame.height}, background);
            unsigned row = y / height;
            if (row < state.rows)
            {
                for (unsigned px = 0; px < w && x + px < state.columns * width; ++px)
                {
                    unsigned column = (x + px) / width;
                    unsigned position = row * state.columns + column;
                    bool selected = viewport.Selected(position);
                    pixels[px] = selected ? 0xff32699a : background;
                    if (TerminalGlyphPixel(font.glyphs, viewport.image.cells[position], (x + px) % width, y % height,
                                           height))
                    {
                        pixels[px] = selected ? 0xffffffff : foreground;
                    }
                }
            }
            unsigned position = state.y * state.columns + state.x;
            if (state.caret && !viewport.selected && viewport.image.firstLine == viewport.image.liveLine &&
                position >= state.caretBack)
            {
                position -= state.caretBack;
                canvas.Fill(
                    {(int)(position % state.columns * width), (int)(position / state.columns * height), 2, (int)height},
                    0xff60cfff);
            }
            unsigned statusY = state.rows * height;
            if (y >= statusY)
            {
                canvas.Fill({0, (int)statusY, (int)frame.width, 16}, 0xff243545);
                char status[160];
                if (notice[0])
                {
                    snprintf(status, sizeof(status), "%s", notice);
                }
                else if (viewport.selected)
                {
                    snprintf(status, sizeof(status),
                             "Selection held | Ctrl+Shift+C copy | Ctrl+Shift+V paste | Esc live");
                }
                else
                {
                    snprintf(status, sizeof(status),
                             "Back %u/%u | Wheel/Shift+PgUp/Dn | Drag select | Ctrl+Shift+C/V copy/paste",
                             (unsigned)(viewport.image.liveLine - viewport.image.firstLine),
                             (unsigned)(viewport.image.liveLine - viewport.image.oldestLine));
                }
                canvas.Text(font, 0, statusY, status, 0xffb0c5d8);
            }
            HackosWindowUpload upload = {frame.generation, x, y, w, 1};
            if (syscall_window_upload(window.Handle(), &upload, pixels))
            {
                syscall_window_frame(window.Handle(), HACKOS_WINDOW_CANCEL, &frame);
                return false;
            }
        }
    }
    if (syscall_window_frame(window.Handle(), HACKOS_WINDOW_PRESENT, &frame))
    {
        syscall_window_frame(window.Handle(), HACKOS_WINDOW_CANCEL, &frame);
        return false;
    }
    return true;
}

int main()
{
    auto fd = syscall_fd_open("~/UTIL/FONTS/zap-light16.psf", HACKOS_OPEN_READ);
    if (fd >= HACKOS_FD_LIMIT)
    {
        return 1;
    }
    syscall_fd_seek(fd, 4);
    for (unsigned i = 0; i < sizeof(font.glyphs); i += 1024)
    {
        if (syscall_fd_read(fd, font.glyphs + i, 1024, 0) != 1024)
        {
            return 1;
        }
    }
    syscall_fd_close(fd);
    HackosWindowSpec spec = {40, 40, 720, 432, "Terminal"};
    if (!window.Open(spec))
    {
        return 1;
    }
    auto shell = syscall_terminal_session(HACKOS_TTY_CREATE, window.Handle(), nullptr);
    if (shell == HACKOS_ERROR || shell == HACKOS_DENIED)
    {
        return 1;
    }
    if (syscall_terminal_session(HACKOS_TTY_SETTINGS, 0, &appearance))
    {
        return 1;
    }
    viewport.Init();
    uint64_t revision = ~0ULL;
    bool dirty = true;
    Print("TERMINAL_READY\n");
    for (;;)
    {
        if (syscall_wait(shell, nullptr, HACKOS_WAIT_NOHANG) == shell)
        {
            break;
        }
        HackosWindowEvent2 event = {};
        for (unsigned n = 0; n < 64 && syscall_window_event2(window.Handle(), &event) == 1; ++n)
        {
            if (event.event.type == HACKOS_WINDOW_CLOSE_REQUEST)
            {
                window.ReplyClose(true);
                return 0;
            }
            if (event.event.type == HACKOS_WINDOW_INPUT_RESET || event.event.type == HACKOS_WINDOW_OVERFLOW)
            {
                HackosWindowInputState input = {};
                if (!syscall_window_input(window.Handle(), HACKOS_INPUT_QUERY, &input))
                {
                    viewport.ResetInput(input.buttons);
                }
                dirty = true;
                continue;
            }
            if (event.event.type == HACKOS_WINDOW_MOUSE)
            {
                if (viewport.Mouse(event.event.x, event.event.y, event.event.buttons, appearance.fontSize / 2,
                                   appearance.fontSize))
                {
                    notice[0] = 0;
                    dirty = true;
                }
                continue;
            }
            if (event.event.type == HACKOS_WINDOW_WHEEL)
            {
                viewport.Scroll(-(int64_t)(int32_t)event.event.key * 3);
                notice[0] = 0;
                dirty = true;
                continue;
            }
            bool down = event.event.type == HACKOS_WINDOW_KEY_DOWN;
            unsigned physical = event.event.key;
            if (down &&
                (event.modifiers & (HACKOS_MOD_CTRL | HACKOS_MOD_SHIFT)) == (HACKOS_MOD_CTRL | HACKOS_MOD_SHIFT) &&
                physical == HACKOS_PHYSICAL_C)
            {
                auto length = viewport.Copy(copied, sizeof(copied));
                if (length == HACKOS_BUFFER_SMALL)
                {
                    snprintf(notice, sizeof(notice), "Selection exceeds 4096 bytes; clipboard unchanged");
                }
                else if (length == HACKOS_ERROR)
                {
                    snprintf(notice, sizeof(notice), "Drag to select text first");
                }
                else if (syscall_clipboard(HACKOS_CLIPBOARD_WRITE, copied, length) != length)
                {
                    snprintf(notice, sizeof(notice), "Copy denied; focus this terminal and retry");
                }
                else
                {
                    snprintf(notice, sizeof(notice), "Copied %u bytes", (unsigned)length);
                }
                dirty = true;
                continue;
            }
            if (down &&
                (event.modifiers & (HACKOS_MOD_CTRL | HACKOS_MOD_SHIFT)) == (HACKOS_MOD_CTRL | HACKOS_MOD_SHIFT) &&
                physical == HACKOS_PHYSICAL_V)
            {
                auto length = syscall_clipboard(HACKOS_CLIPBOARD_READ, copied, sizeof(copied));
                if (length > sizeof(copied))
                {
                    snprintf(notice, sizeof(notice), "Paste denied; focus this terminal and retry");
                }
                else if (!length)
                {
                    snprintf(notice, sizeof(notice), "Clipboard is empty");
                }
                else
                {
                    auto result = syscall_terminal_session(HACKOS_TTY_PASTE, length, copied);
                    if (result == HACKOS_WOULD_BLOCK)
                    {
                        snprintf(notice, sizeof(notice), "Input busy; retry paste after pending input is read");
                    }
                    else if (result)
                    {
                        snprintf(notice, sizeof(notice), "Paste requires ASCII text; control bytes are not supported");
                    }
                    else
                    {
                        viewport.Live();
                        snprintf(notice, sizeof(notice),
                                 "Pasted text; tabs/line breaks become spaces. Enter to submit.");
                    }
                }
                for (auto& byte : copied)
                {
                    byte = 0;
                }
                dirty = true;
                continue;
            }
            if (down && (event.modifiers & HACKOS_MOD_SHIFT) &&
                (physical == 0xc9 || physical == 0xd1 || physical == 0xc7 || physical == 0xcf))
            {
                if (physical == 0xcf)
                {
                    viewport.Live();
                }
                else
                {
                    int64_t amount = viewport.ready ? viewport.image.state.rows : 1;
                    viewport.Scroll(physical == 0xc7 ? HACKOS_TTY_HISTORY_ROWS : physical == 0xc9 ? amount : -amount);
                }
                notice[0] = 0;
                dirty = true;
                continue;
            }
            if (down && physical == HACKOS_PHYSICAL_ESCAPE &&
                (viewport.selected || viewport.requested != HACKOS_TTY_LIVE))
            {
                viewport.Live();
                notice[0] = 0;
                dirty = true;
                continue;
            }
            auto key = Key(event);
            if (key)
            {
                dirty |= viewport.selected || viewport.requested != HACKOS_TTY_LIVE;
                viewport.Live();
                notice[0] = 0;
                syscall_terminal_session(HACKOS_TTY_INPUT, key, nullptr);
            }
            if (event.event.type == HACKOS_WINDOW_RESIZE || event.event.type == HACKOS_WINDOW_MODE)
            {
                dirty = true;
            }
        }

        HackosWindowState geometry = {};
        if (syscall_window_state(window.Handle(), &geometry))
        {
            break;
        }
        HackosTerminalSettings next = {};
        if (syscall_terminal_session(HACKOS_TTY_SETTINGS, 0, &next))
        {
            break;
        }
        if (next.fontSize != appearance.fontSize || next.bgColour != appearance.bgColour ||
            next.fontColour != appearance.fontColour)
        {
            viewport.Live();
            notice[0] = 0;
            dirty = true;
        }
        appearance = next;
        unsigned cols = geometry.width / (appearance.fontSize / 2);
        unsigned rows = geometry.height > 16 ? (geometry.height - 16) / appearance.fontSize : 1;
        if (cols > 160)
        {
            cols = 160;
        }
        if (rows > 60)
        {
            rows = 60;
        }
        if (cols < 1)
        {
            cols = 1;
        }
        if (rows < 1)
        {
            rows = 1;
        }
        syscall_terminal_session(HACKOS_TTY_RESIZE, cols | (rows << 16), nullptr);
        HackosTerminalState state = {};
        if (syscall_terminal_session(HACKOS_TTY_STATE, 0, &state))
        {
            break;
        }
        if ((dirty || state.revision != revision) &&
            !(syscall_window_mode(window.Handle(), HACKOS_WINDOW_QUERY) & HACKOS_WINDOW_MINIMIZED))
        {
            if (!syscall_terminal_session(HACKOS_TTY_VIEW, viewport.requested, &snapshot))
            {
                if (viewport.ready && viewport.image.epoch != snapshot.epoch)
                {
                    viewport.Live();
                    notice[0] = 0;
                    if (syscall_terminal_session(HACKOS_TTY_VIEW, HACKOS_TTY_LIVE, &snapshot))
                    {
                        window.Wait(32);
                        continue;
                    }
                }
                revision = snapshot.state.revision;
                dirty |= viewport.Accept(snapshot);
                if (dirty && Paint())
                {
                    dirty = false;
                }
            }
        }

        window.Wait(32);
    }
    window.Close();
    return 0;
}
