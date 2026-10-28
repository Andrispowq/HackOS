#include "hackos/user.h"
#include "interaction.h"

static bool interaction;
static InteractionDemo interactionDemo;

static bool trace;

static bool Paint(uint64_t window, uint32_t accent, int32_t mouseX, int32_t mouseY)
{
    HackosWindowFrame state = {};
    if (syscall_window_frame(window, HACKOS_WINDOW_BEGIN, &state) != 0)
    {
        return false;
    }

    uint32_t pixels[1024];
    uint32_t tileWidth = state.width < 1024 ? state.width : 1024;
    uint32_t stripHeight = 1024 / tileWidth;
    for (uint32_t firstY = 0; firstY < state.height; firstY += stripHeight)
    {
        uint32_t height = state.height - firstY;
        if (height > stripHeight)
        {
            height = stripHeight;
        }

        for (uint32_t firstX = 0; firstX < state.width; firstX += tileWidth)
        {
            uint32_t width = state.width - firstX;
            if (width > tileWidth)
            {
                width = tileWidth;
            }

            for (uint32_t row = 0; row < height; row++)
            {
                uint32_t y = firstY + row;
                for (uint32_t column = 0; column < width; column++)
                {
                    uint32_t x = firstX + column;
                    uint32_t colour = 0xff000000 | (x * 255 / state.width) << 16 | (y * 255 / state.height) << 8 | 0x60;
                    if (x >= 10 && x < 30 && y >= 10 && y < 30)
                    {
                        colour = accent;
                    }

                    if ((int32_t)x >= mouseX - 2 && (int32_t)x <= mouseX + 2 && (int32_t)y >= mouseY - 2 &&
                        (int32_t)y <= mouseY + 2)
                    {
                        colour = 0xffffffff;
                    }

                    pixels[row * width + column] = interaction ? interactionDemo.Pixel(x, y) : colour;
                }
            }

            HackosWindowUpload rect = {state.generation, firstX, firstY, width, height};
            // Maximize can exceed one upload's width. Tile both dimensions,
            // and retry from a fresh snapshot if geometry changes mid-paint.
            if (syscall_window_upload(window, &rect, pixels) != 0)
            {
                return false;
            }
        }
    }

    return syscall_window_frame(window, HACKOS_WINDOW_PRESENT, &state) == 0;
}

static void Log(const char* label, int64_t first, uint64_t second)
{
    if (!trace)
    {
        return;
    }
    char line[80];
    unsigned length = 0;
    while (label[length])
    {
        line[length] = label[length];
        length++;
    }

    if (first < 0)
    {
        line[length++] = '-';
    }

    uint64_t magnitude = first < 0 ? 0 - (uint64_t)first : (uint64_t)first;
    uint64_t values[] = {magnitude, second};
    for (auto value : values)
    {
        char digits[21];
        unsigned count = 0;
        do
        {
            digits[count++] = '0' + value % 10;
            value /= 10;
        } while (value);

        while (count)
        {
            line[length++] = digits[--count];
        }

        line[length++] = ' ';
    }

    line[length - 1] = '\n';
    syscall_user_write(line, length);
}

int WidgetDemo(bool trace);

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (User::Equal(argv[i], "--widgets"))
        {
            bool tracing = false;
            for (int j = 1; j < argc; ++j)
            {
                tracing |= User::Equal(argv[j], "--trace");
            }
            return WidgetDemo(tracing);
        }
    }
    HackosWindowSpec spec = {200, 200, 200, 170, "Window input demo"};
    bool second = false;
    for (int i = 1; i < argc; ++i)
    {
        if (User::Equal(argv[i], "--trace"))
        {
            trace = true;
        }
        if (User::Equal(argv[i], "--interaction"))
        {
            interaction = true;
            spec.width = 640;
            spec.height = 280;
            interactionDemo.LoadFont();
        }
        if (User::Equal(argv[i], "second"))
        {
            second = true;
        }
    }
    if (second)
    {
        spec.x = 450;
        spec.y = 250;
    }

    uint64_t window = syscall_window_create(&spec);
    if (window == HACKOS_ERROR)
    {
        User::Print("Cannot create window.\n");
        return 1;
    }

    HackosWindowInputState inputState;
    if (syscall_window_input(window, HACKOS_INPUT_ENABLE, &inputState) != 0)
    {
        syscall_window_close(window);
        return 1;
    }
    uint32_t accent = 0xffff00ff;
    int32_t scroll = 0;
    int32_t mouseX = -10, mouseY = -10;
    bool repaint = !Paint(window, accent, mouseX, mouseY);
    if (interaction)
    {
        User::Print("Window demo ready. Tab/Shift-Tab or click selects a field.\n"
                    "Ctrl-A selects text; Ctrl-C/X/V copies, cuts and pastes. Ctrl-S saves.\n"
                    "Alt-F4 requests close; unsaved edits can be saved, discarded or kept.\n");
    }
    else
    {
        User::Print(
            "Window demo ready. Input test: click/drag in the client to place the white marker.\n"
            "The gradient is a test pattern, not a colour picker. Type or scroll to recolour the small swatch.\n"
            "Drag title, resize bottom-right; M minimizes, F maximizes/restores, R restores, Q closes.\n");
    }

    unsigned pending = 0;

    while (syscall_window_poll(window) == 0)
    {
        HackosWindowEvent2 input = {};
        uint64_t result = syscall_window_event2(window, &input);
        auto event = input.event;
        if (result == HACKOS_ERROR)
        {
            break;
        }

        if (result == 1)
        {
            if (interaction && !interactionDemo.Event(window, input, repaint))
            {
                break;
            }
            if (event.type == HACKOS_WINDOW_CLOSE_REQUEST && !interaction)
            {
                syscall_window_close_reply(window, 1);
                break;
            }
            if (event.type == HACKOS_WINDOW_TEXT)
            {
                event.type = HACKOS_WINDOW_KEY;
            }
            if (event.type == HACKOS_WINDOW_KEY && !interaction)
            {
                if (event.key == 'q' || event.key == 'Q')
                {
                    break;
                }

                if (event.key == 'm' || event.key == 'f' || event.key == 'r')
                {
                    uint64_t mode = syscall_window_mode(window, HACKOS_WINDOW_QUERY);
                    uint64_t operation = HACKOS_WINDOW_RESTORE;
                    if (event.key == 'm')
                    {
                        operation = HACKOS_WINDOW_MINIMIZE;
                    }

                    if (event.key == 'f' && !(mode & HACKOS_WINDOW_MAXIMIZED))
                    {
                        operation = HACKOS_WINDOW_MAXIMIZE;
                    }

                    if (syscall_window_mode(window, operation) == HACKOS_ERROR)
                    {
                        User::Print("Window mode request rejected.\n");
                    }

                    continue;
                }

                accent = 0xff000000 | (event.key * 0x12345 & 0xffffff);
                Log("WINDOW_KEY ", event.key, 0);
                repaint = true;
            }
            else if (event.type == HACKOS_WINDOW_MOUSE && !interaction)
            {
                if (event.buttons & 1)
                {
                    mouseX = event.x;
                    mouseY = event.y;
                    Log("WINDOW_MOUSE ", (uint32_t)mouseX, (uint32_t)mouseY);
                    repaint = true;
                }
            }
            else if (event.type == HACKOS_WINDOW_WHEEL)
            {
                int32_t delta = (int32_t)event.key;
                scroll += delta;
                if (scroll < -255)
                {
                    scroll = -255;
                }

                if (scroll > 255)
                {
                    scroll = 255;
                }

                accent = 0xff000000 | ((uint32_t)(scroll + 255) * 0x12345 & 0xffffff);
                Log("WINDOW_WHEEL ", delta, 0);
                repaint = true;
            }
            else if (event.type == HACKOS_WINDOW_MODE)
            {
                Log("WINDOW_MODE ", event.key, 0);
                if (!(event.key & HACKOS_WINDOW_MINIMIZED))
                {
                    repaint = true;
                }
            }
            else if (event.type == HACKOS_WINDOW_RESIZE)
            {
                Log("WINDOW_RESIZE ", event.width, event.height);
                repaint = true;
            }
            else if (event.type == HACKOS_WINDOW_MOVE)
            {
                Log("WINDOW_MOVE ", event.x, event.y);
            }
            else if (event.type == HACKOS_WINDOW_FOCUS)
            {
                Log("WINDOW_FOCUS ", event.focused, 0);
            }
            else if (event.type == HACKOS_WINDOW_OVERFLOW || event.type == HACKOS_WINDOW_INPUT_RESET)
            {
                syscall_window_input(window, HACKOS_INPUT_QUERY, &inputState);
                HackosWindowState state;
                if (syscall_window_state(window, &state) != 0)
                {
                    break;
                }

                mouseX = state.mouseX;
                mouseY = state.mouseY;
                if (trace)
                {
                    User::Print("WINDOW_OVERFLOW: state restored\n");
                }
                repaint = true;
            }
        }

        // Coalesce a bounded batch, but never let continuous pointer input
        // starve painting after resize has cleared the client buffer.
        if (result == 1 && ++pending < 16)
        {
            continue;
        }

        pending = 0;
        if (repaint && !(syscall_window_mode(window, HACKOS_WINDOW_QUERY) & HACKOS_WINDOW_MINIMIZED))
        {
            repaint = !Paint(window, accent, mouseX, mouseY);
            if (!repaint)
            {
                HackosWindowState state;
                if (syscall_window_state(window, &state) == 0)
                {
                    Log("WINDOW_PAINT ", state.width, state.height);
                }
            }
        }

        if (repaint && !(syscall_window_mode(window, HACKOS_WINDOW_QUERY) & HACKOS_WINDOW_MINIMIZED))
        {
            // Retry transient pressure without spinning or sleeping under a guard.
            syscall_sleep(16);
        }
        else
        {
            HackosPollFd ready = {window, HACKOS_POLL_WINDOW | HACKOS_POLL_READ, 0};
            syscall_poll(&ready, 1, HACKOS_WAIT_FOREVER);
        }
    }

    syscall_window_close(window);
    User::Print("Window demo closed.\n");
    return 0;
}
