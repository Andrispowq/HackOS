#include "hackos/syscall.h"
extern "C" int context_probe();
extern "C"
{
    volatile uint64_t release_gate;
    volatile uint64_t private_value;
    alignas(4096) char buffer[8192];
}

static uint64_t write(const char* p, uint64_t n)
{
    return MakeSyscall(HACKOS_SYS_user_write, (uint64_t)p, n, 0, 0, 0);
}

extern "C" [[noreturn]] void _start()
{
    uint64_t flags;
    uint16_t cs, ss;
    asm volatile("mov %%cs,%0; mov %%ss,%1; pushfq; popq %2" : "=r"(cs), "=r"(ss), "=r"(flags));
    bool ok = (cs & 3) == 3 && (ss & 3) == 3 && (flags & 0x200) && !(flags & 0x3000);
    ok = ok && !release_gate && !private_value && !buffer[0] && !buffer[8191];
    private_value = 0x12345678;
    for (uint64_t nr = 1; nr < 14; ++nr)
    {
        ok = MakeSyscall(nr, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL) == ~0ULL && ok;
    }

    ok = MakeSyscall(999, 0, 0, 0, 0, 0) == ~0ULL && ok;
    ok = write((char*)0x100000, 16) == ~0ULL && ok;       // supervisor pointer
    ok = write((char*)0x400000100000, 16) == ~0ULL && ok; // unmapped user hole
    ok = write((char*)0x4000001fffff, 2) == ~0ULL && ok;  // crosses arena end
    ok = write((char*)0x800000000000, 16) == ~0ULL && ok; // noncanonical
    ok = write((char*)(~0ULL - 7), 16) == ~0ULL && ok;    // wraps
    ok = write(buffer, 1025) == ~0ULL && ok;
    ok = write(nullptr, 0) == 0 && ok;
    buffer[4094] = 'x';
    buffer[4095] = 'y';
    buffer[4096] = 'z';
    buffer[4097] = '\n';
    ok = write(buffer + 4094, 4) == 4 && ok; // valid page-spanning copy
    ok = context_probe() == 0 && ok;
    const char ready[] = "RING3_PROBE_READY\n";
    const char failed[] = "RING3_PROBE_FAIL\n";
    if (!ok)
    {
        write(failed, sizeof(failed) - 1);
        MakeSyscall(HACKOS_SYS_exit, 1, 0, 0, 0, 0);
    }

    write(ready, sizeof(ready) - 1);
    while (!release_gate)
    {
        asm volatile("pause" ::: "memory");
    }

    ok = private_value == 0x12345678;
    const char done[] = "RING3_PROBE_OK\n";
    write(ok ? done : failed, ok ? sizeof(done) - 1 : sizeof(failed) - 1);
    MakeSyscall(HACKOS_SYS_exit, ok ? 0 : 2, 0, 0, 0, 0);
    for (;;)
    {
        asm volatile("ud2");
    }
}
