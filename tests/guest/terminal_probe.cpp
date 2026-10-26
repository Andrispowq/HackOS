#include "hackos/user.h"
int main()
{
    if (syscall_input() != 0 || syscall_open("~/USR/HELLO.TXT") == HACKOS_ERROR)
    { User::Print("PROBE_FAIL: terminal handoff\n"); return 1; }
    User::Print("TERMINAL_SPIN_READY\n");
    // Never reads input again: only the kernel can stop this loop on Ctrl-C.
    // Leave a file open to exercise interrupt termination resource cleanup.
    for (;;) asm volatile("pause");
}
