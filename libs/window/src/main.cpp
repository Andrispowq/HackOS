#include "hackos/user.h"

static bool Paint(uint64_t window, uint32_t accent, int32_t mouseX, int32_t mouseY)
{
    HackosWindowState state;
    if (syscall_window_state(window, &state) != 0) return false;

    uint32_t pixels[1024];
    uint32_t stripHeight = 1024 / state.width;
    for (uint32_t firstY = 0; firstY < state.height; firstY += stripHeight)
    {
        uint32_t height = state.height - firstY;
        if (height > stripHeight) height = stripHeight;

        for (uint32_t row = 0; row < height; row++)
        {
            uint32_t y = firstY + row;
            for (uint32_t x = 0; x < state.width; x++)
            {
                uint32_t colour = 0xff000000 | (x * 255 / state.width) << 16 | (y * 255 / state.height) << 8 | 0x60;
                if (x >= 10 && x < 30 && y >= 10 && y < 30)
                {
                    colour = accent;
                }

                if ((int32_t)x >= mouseX - 2 && (int32_t)x <= mouseX + 2 &&
                    (int32_t)y >= mouseY - 2 && (int32_t)y <= mouseY + 2)
                {
                    colour = 0xffffffff;
                }

                pixels[row * state.width + x] = colour;
            }
        }

        HackosWindowRect rect = {0, firstY, state.width, height};
        // Geometry may change between uploads. Retry from a fresh snapshot.
        if (syscall_window_blit(window, &rect, pixels) != 0) return false;
    }

    return true;
}

static void Log(const char* label, uint64_t first, uint64_t second)
{
    char line[80];
    unsigned length = 0;
    while (label[length])
    {
        line[length] = label[length];
        length++;
    }

    uint64_t values[] = {first, second};
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

int main(int argc, char** argv)
{
    HackosWindowSpec spec = {200, 200, 200, 170, "Window input demo"};
    if (argc > 1 && User::Equal(argv[1], "second"))
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

    uint32_t accent = 0xffff00ff;
    int32_t mouseX = -10, mouseY = -10;
    bool repaint = !Paint(window, accent, mouseX, mouseY);
    User::Print("Window demo ready. Drag title, resize bottom-right, click to focus. Type to recolour, click to mark, Q to close.\n");

    while (syscall_window_poll(window) == 0)
    {
        HackosWindowEvent event;
        uint64_t result = syscall_window_event(window, &event);
        if (result == HACKOS_ERROR) break;

        if (result == 1)
        {
            if (event.type == HACKOS_WINDOW_KEY)
            {
                if (event.key == 'q' || event.key == 'Q') break;

                accent = 0xff000000 | (event.key * 0x12345 & 0xffffff);
                Log("WINDOW_KEY ", event.key, 0);
                repaint = true;
            }
            else if (event.type == HACKOS_WINDOW_MOUSE)
            {
                if (event.buttons & 1)
                {
                    mouseX = event.x;
                    mouseY = event.y;
                    Log("WINDOW_MOUSE ", (uint32_t)mouseX, (uint32_t)mouseY);
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
            else if (event.type == HACKOS_WINDOW_OVERFLOW)
            {
                HackosWindowState state;
                if (syscall_window_state(window, &state) != 0) break;

                mouseX = state.mouseX;
                mouseY = state.mouseY;
                User::Print("WINDOW_OVERFLOW: state restored\n");
                repaint = true;
            }
        }
        else if (repaint)
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

        syscall_yield();
    }

    syscall_window_close(window);
    User::Print("Window demo closed.\n");
    return 0;
}
