#include "hackos/user.h"
int main()
{
    // Exercise automatic handle cleanup when killed by another process.
    if (syscall_open("~/USR/HELLO.TXT") == HACKOS_ERROR) return 1;
    User::Print("WAIT_PROBE_READY\n");
    for (;;) asm volatile("pause");
}
