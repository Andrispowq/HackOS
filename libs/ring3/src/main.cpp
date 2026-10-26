#include "hackos/syscall.h"

static volatile uint64_t counter;
extern "C" [[noreturn]] void _start()
{
    const char hello[] = "RING3: hello from isolated userspace\n";
    MakeSyscall(HACKOS_SYS_user_write, (uint64_t)hello, sizeof(hello) - 1, 0, 0, 0);
    // Pure user instructions: the timer must preempt this CPU-bound work.
    for (counter = 0; counter < 10000000; ++counter) asm volatile("" ::: "memory");
    const char done[] = "RING3: finished; requesting exit\n";
    MakeSyscall(HACKOS_SYS_user_write, (uint64_t)done, sizeof(done) - 1, 0, 0, 0);
    MakeSyscall(HACKOS_SYS_exit, 0, 0, 0, 0, 0);
    for (;;) asm volatile("ud2");
}
