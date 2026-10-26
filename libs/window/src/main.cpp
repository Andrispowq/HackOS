#include "hackos/user.h"

int main()
{
    HackosWindowSpec spec = {200, 200, 200, 170, "Yo yo yo whats up"};
    uint64_t window = syscall_window_create(&spec);
    if (window == HACKOS_ERROR)
    {
        User::Print("Cannot create window.\n");
        return 1;
    }

    // Paint in small strips. Pixels belong to this process; only copies cross
    // the syscall boundary, never a kernel framebuffer or object pointer.
    uint32_t pixels[200];
    for (uint32_t y = 0; y < spec.height; y++)
    {
        for (uint32_t x = 0; x < spec.width; x++)
        {
            pixels[x] = 0xff000000 | (x * 255 / spec.width) << 16 | (y * 255 / spec.height) << 8 | 0x60;
            if (x >= 10 && x < 30 && y >= 10 && y < 30)
            {
                pixels[x] = 0xffff00ff;
            }
        }

        HackosWindowRect row = {0, y, spec.width, 1};
        if (syscall_window_blit(window, &row, pixels) != 0)
        {
            syscall_window_close(window);
            User::Print("Cannot draw window.\n");
            return 1;
        }
    }

    User::Print("Window demo ready. Close X, press Q, or use Ctrl-C.\n");

    while (syscall_window_poll(window) == 0)
    {
        uint64_t key = syscall_input();
        if (key == 'q' || key == 'Q') break;

        syscall_yield();
    }

    syscall_window_close(window);
    User::Print("Window demo closed.\n");
    return 0;
}
