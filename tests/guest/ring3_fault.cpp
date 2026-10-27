#include "hackos/syscall.h"

extern "C" [[noreturn]] void _start()
{
#if FAULT == 0
    asm volatile("mov (%%rax), %%rax" : : "a"(0x100000) : "memory");
#elif FAULT == 1
    *(volatile uint64_t*)0x100000 = 0;
#elif FAULT == 2
    *(volatile char*)_start = 0; // read-only text
#elif FAULT == 3
    unsigned char code[] = {0xc3};
    ((void (*)())code)(); // NX stack
#elif FAULT == 4
    asm volatile("outb %%al, %%dx" : : "a"(0), "d"(0xe9));
#elif FAULT == 5
    asm volatile("cli");
#elif FAULT == 6
    asm volatile("int $0x81"); // kernel-only voluntary yield
#elif FAULT == 7
    asm volatile("syscall"); // fast syscall disabled until implemented
#elif FAULT == 8
    asm volatile("mov $0x4000001fc000, %%rax; mov %%rax, %%rsp; push %%rax" ::: "rax", "memory");
#endif
    const char failed[] = "RING3_PROBE_FAIL: forbidden instruction completed\n";
    MakeSyscall(HACKOS_SYS_user_write, (uint64_t)failed, sizeof(failed) - 1, 0, 0, 0);
    MakeSyscall(HACKOS_SYS_exit, 1, 0, 0, 0, 0);
    for (;;)
    {
        asm volatile("ud2");
    }
}
