#include "hackos/user.h"
extern "C"
{
    volatile uint64_t release_gate;
}

int main(int argc, char** argv)
{
    if (!argv || argc < 1 || argv[argc] || !User::Equal(argv[0], "~/USR/BIN/ARGPROBE"))
    {
        User::Print("PROBE_FAIL: argv ABI\n");
        return 1;
    }

    for (int i = 0; i < argc; ++i)
    {
        if ((uint64_t)argv[i] < 0x4000001fc000 || (uint64_t)argv[i] >= 0x400000200000)
        {
            User::Print("PROBE_FAIL: argv not private\n");
            return 1;
        }
    }

    if (argc == 1)
    {
        return 19;
    }

    if (User::Equal(argv[1], "quick"))
    {
        return 23;
    }

    if (User::Equal(argv[1], "fault"))
    {
        asm volatile("ud2");
    }

    if (User::Equal(argv[1], "copy"))
    {
        for (unsigned i = 0; i < 20; ++i)
        {
            syscall_yield();
        }

        return argc == 3 && User::Equal(argv[2], "original") ? 31 : 1;
    }

    if (User::Equal(argv[1], "orphan"))
    {
        syscall_spawn("~/USR/BIN/ARGPROBE");
        return 41;
    }

    bool ok = argc == 6 && User::Equal(argv[1], "alpha") && User::Equal(argv[2], "two words") &&
              User::Equal(argv[3], "") && User::Equal(argv[4], "escaped space") && User::Equal(argv[5], "&");
    if (!ok)
    {
        User::Print("PROBE_FAIL: shell parsing\n");
        return 1;
    }

    User::Print("ARGS_READY\n");
    while (!release_gate)
    {
        asm volatile("pause" ::: "memory");
    }

    User::Print("ARGS_DONE\n");
    return 7;
}
