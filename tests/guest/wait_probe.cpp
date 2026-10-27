#include "hackos/user.h"

int main()
{
    if (syscall_input() != HACKOS_DENIED || syscall_console(0) != HACKOS_DENIED)
    {
        User::Print("PROBE_FAIL: background stole input\n");
        return 1;
    }

    // Exercise automatic handle cleanup when killed by another process.
    if (syscall_open("~/USR/HELLO.TXT") == HACKOS_ERROR)
    {
        return 1;
    }

    User::Print("WAIT_PROBE_READY\n");
    for (;;)
    {
        asm volatile("pause");
    }
}
