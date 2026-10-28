#include "hackos/user.h"
#define REQUIRE(x)                                                                                                     \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            User::Print("PROBE_FAIL: " #x "\n");                                                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main(int argc, char**)
{
    if (argc > 1)
    {
        REQUIRE(syscall_terminal_cursor(0, 1) == HACKOS_DENIED);
        User::Print("CURSOR_DENIED_OK\n");
        return 0;
    }
    REQUIRE(syscall_terminal_cursor(256, 1) == HACKOS_ERROR);
    REQUIRE(syscall_terminal_cursor(0, 2) == HACKOS_ERROR);
    REQUIRE(syscall_terminal_cursor(0, 1) == 0);
    REQUIRE(syscall_terminal_cursor(0, 0) == 0);
    HackosFileInfo entries[16], single;
    REQUIRE(syscall_readdir_batch("~/USR/BIN", 0, entries, 0) == HACKOS_ERROR);
    REQUIRE(syscall_readdir_batch("~/USR/BIN", 0, entries, 17) == HACKOS_ERROR);
    REQUIRE(syscall_readdir_batch("~/USR/BIN", 0, (HackosFileInfo*)0, 1) == HACKOS_ERROR);
    REQUIRE(syscall_readdir_batch("~/USR/BIN", 0, (HackosFileInfo*)0x400000000000, 1) == HACKOS_ERROR);
    REQUIRE(syscall_readdir_batch("~/USR/BIN", ~0ULL, entries, 16) == 0);
    uint64_t index = 0;
    for (;;)
    {
        uint64_t count = syscall_readdir_batch("~/USR/BIN", index, entries, 16);
        REQUIRE(count <= 16);
        if (!count)
        {
            break;
        }
        for (uint64_t i = 0; i < count; ++i)
        {
            REQUIRE(syscall_readdir("~/USR/BIN", index + i, &single) == 1);
            REQUIRE(User::Equal(entries[i].name, single.name));
            REQUIRE(entries[i].size == single.size && entries[i].mode == single.mode);
            REQUIRE(entries[i].uid == single.uid && entries[i].gid == single.gid);
        }
        index += count;
    }
    REQUIRE(index > 16);
    User::Print("CONSOLE_ABI_OK\n");
    return 0;
}
