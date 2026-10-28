#include "hackos/user.h"

static uint64_t Ticks()
{
    HackosClock clock = {};
    syscall_clock(&clock);
    return clock.ticks;
}

static void Report(const char* name, uint64_t value)
{
    User::Print(name);
    User::Number(value);
    User::Print("\n");
}

int main()
{
    char text[8192];
    uint64_t used = 0, count = 0;
    uint64_t start = Ticks();
    for (;; ++count)
    {
        HackosFileInfo info = {};
        uint64_t result = syscall_readdir("~/USR/BIN", count, &info);
        if (!result)
        {
            break;
        }
        if (result != 1)
        {
            return 1;
        }
        uint64_t length = User::Length(info.name);
        if (used + length + 1 > sizeof(text))
        {
            return 2;
        }
        for (uint64_t i = 0; i < length; ++i)
        {
            text[used++] = info.name[i];
        }
        text[used++] = '\n';
    }
    uint64_t enumeration = Ticks() - start;
    start = Ticks();
    for (uint64_t first = 0, i = 0; i < used; ++i)
    {
        if (text[i] == '\n')
        {
            User::Write(1, text + first, i - first);
            User::Write(1, "\n", 1);
            first = i + 1;
        }
    }
    uint64_t small = Ticks() - start;
    start = Ticks();
    User::Write(1, text, used);
    uint64_t grouped = Ticks() - start;
    Report("LIST_ENTRIES=", count);
    Report("ENUM_TICKS=", enumeration);
    Report("SMALL_WRITE_TICKS=", small);
    Report("GROUPED_WRITE_TICKS=", grouped);
    User::Print("LISTING_PROBE_OK\n");
    return 0;
}
