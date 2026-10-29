#include "hackos/gui.h"
#include "hackos/user.h"
using namespace User;
static Gui::Window window;
static Gui::Font font;
static char cells[HACKOS_TTY_CELLS];
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

static bool Paint(const HackosTerminalState& state)
{
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
            canvas.Fill({0, 0, (int)frame.width, (int)frame.height}, 0xff101b25);
            unsigned row = y / 16;
            if (row < state.rows)
            {
                char line[161] = {};
                for (unsigned col = 0; col < state.columns; ++col)
                {
                    line[col] = cells[row * state.columns + col];
                }
                canvas.Text(font, 0, row * 16, line, 0xffe5eef5);
            }
            unsigned position = state.y * state.columns + state.x;
            if (state.caret && position >= state.caretBack)
            {
                position -= state.caretBack;
                canvas.Fill({(int)(position % state.columns * 8), (int)(position / state.columns * 16), 2, 16},
                            0xff60cfff);
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
            auto key = Key(event);
            if (key)
            {
                syscall_terminal_session(HACKOS_TTY_INPUT, key, nullptr);
            }
            dirty = true;
        }
        HackosWindowState geometry = {};
        if (syscall_window_state(window.Handle(), &geometry))
        {
            break;
        }
        unsigned cols = geometry.width / 8, rows = geometry.height / 16;
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
            auto copied = syscall_terminal_session(HACKOS_TTY_CELLS_READ, 0, cells);
            // A writer between snapshots is retried; only a coherent grid is rendered.
            if (copied == state.revision && Paint(state))
            {
                revision = copied;
                dirty = false;
            }
        }
        window.Wait(32);
    }
    window.Close();
    return 0;
}
