#include "hackos/user.h"
#include "hackos/stdio.h"
using namespace User;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("FILTER_PROBE_FAIL %u\n", __LINE__);                                                                \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

int main(int argc, char** argv)
{
    if (argc == 2 && Equal(argv[1], "child"))
    {
        HackosTerminalInfo info = {};
        CHECK(syscall_terminal_info(2, &info) == 0 && (info.flags & HACKOS_TERMINAL));
        CHECK(!(info.flags & HACKOS_TERMINAL_FOREGROUND));
        CHECK(syscall_terminal_read(HACKOS_INPUT_NONBLOCK) == HACKOS_DENIED);
        char ch;
        CHECK(syscall_fd_read(0, &ch, 1, 0) == HACKOS_DENIED);
        HackosPollFd poll = {0, HACKOS_POLL_READ, 0};
        CHECK(syscall_poll(&poll, 1, 0) == 1 && poll.revents == HACKOS_POLL_ERROR);
        return 0;
    }
    if (argc == 2 && Equal(argv[1], "children"))
    {
        const char* args[] = {"~/USR/BIN/FILTERPROBE", "child"};
        auto pid = syscall_spawn_args(args[0], 2, args);
        CHECK(pid != HACKOS_ERROR && pid != HACKOS_DENIED);
        int64_t status = -1;
        CHECK(syscall_wait(pid, &status, 0) == pid && status == 0);
        Print("FILTER_CHILD_DENIED\n");
        return 0;
    }
    if (argc == 2 && Equal(argv[1], "source"))
    {
        char line[] = "row0000\n";
        for (unsigned i = 0; i < 3000; ++i)
        {
            unsigned value = i;
            for (int pos = 6; pos >= 3; --pos)
            {
                line[pos] = '0' + value % 10;
                value /= 10;
            }
            if (!Write(1, line, 8))
            {
                return 1;
            }
        }
        return 0;
    }
    if (argc == 2 && Equal(argv[1], "binary"))
    {
        char buffer[1024];
        for (unsigned part = 0; part < 12; ++part)
        {
            for (unsigned i = 0; i < sizeof(buffer); ++i)
            {
                buffer[i] = (part * sizeof(buffer) + i) % 251;
            }
            CHECK(Write(1, buffer, sizeof(buffer)));
        }
        return 0;
    }

    HackosTerminalInfo info = {};
    CHECK(syscall_terminal_info(1, &info) == 0 && info.columns && info.rows && !info.reserved);
    CHECK(info.flags & HACKOS_TERMINAL);
    if (argc == 2 && Equal(argv[1], "background"))
    {
        CHECK(!(info.flags & HACKOS_TERMINAL_FOREGROUND));
        CHECK(syscall_terminal_read(0) == HACKOS_DENIED);
        char ch;
        CHECK(syscall_fd_read(0, &ch, 1, 0) == HACKOS_DENIED);
        Print("FILTER_BACKGROUND_DENIED\n");
        return 0;
    }
    CHECK(info.flags & HACKOS_TERMINAL_FOREGROUND);
    CHECK(syscall_terminal_info(16, &info) == HACKOS_ERROR);
    CHECK(syscall_terminal_info(~0ULL, &info) == HACKOS_ERROR);
    CHECK(syscall_terminal_info(1, (HackosTerminalInfo*)"readonly") == HACKOS_ERROR);
    CHECK(syscall_terminal_info(1, (HackosTerminalInfo*)0x12345) == HACKOS_ERROR);
    CHECK(syscall_terminal_info(1, (HackosTerminalInfo*)0x4000001fffff) == HACKOS_ERROR);
    uint64_t ends[2];
    CHECK(syscall_pipe(ends) == 0);
    for (auto fd : ends)
    {
        info = {1, 2, 3, 4};
        CHECK(syscall_terminal_info(fd, &info) == 0);
        CHECK(!info.columns && !info.rows && !info.flags && !info.reserved);
        CHECK(syscall_fd_close(fd) == 0);
        CHECK(syscall_terminal_info(fd, &info) == HACKOS_ERROR);
    }
    auto file = syscall_fd_open("/usr/hello.txt", HACKOS_OPEN_READ);
    CHECK(file < HACKOS_FD_LIMIT);
    info = {1, 2, 3, 4};
    CHECK(syscall_terminal_info(file, &info) == 0);
    CHECK(!info.columns && !info.rows && !info.flags && !info.reserved);
    CHECK(syscall_fd_close(file) == 0);
    Print("FILTER_ABI_OK\n");
    return 0;
}
