#include "hackos/user.h"
#include "hackos/stdio.h"
using namespace User;

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            printf("TERMINAL_PROBE_FAIL %u\n", __LINE__);                                                              \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

static bool Same(const HackosTerminalSettings& a, const HackosTerminalSettings& b)
{
    return a.fontSize == b.fontSize && a.bgColour == b.bgColour && a.fontColour == b.fontColour &&
           a.columns == b.columns && a.rows == b.rows && a.reserved == b.reserved;
}

static int Command(const char* operation, const char* key, const char* value, int expected)
{
    const char* args[] = {"/usr/bin/terminal", operation, key, value};
    auto pid = syscall_spawn_foreground(args[0], value ? 4 : key ? 3 : 2, args);
    CHECK(pid != HACKOS_ERROR && pid != HACKOS_DENIED);
    int64_t status = -1;
    CHECK(syscall_wait(pid, &status, 0) == pid && status == expected);
    return 0;
}

int main(int argc, char** argv)
{
    HackosTerminalSettings initial = {}, after = {};
    CHECK(syscall_terminal_get(&initial) == 0 && initial.columns && initial.rows && !initial.reserved);
    if (argc == 2 && Equal(argv[1], "background"))
    {
        CHECK(syscall_terminal_set(HACKOS_TERMINAL_FONT_SIZE, 64) == HACKOS_DENIED);
        CHECK(syscall_terminal_set(HACKOS_TERMINAL_BG_COLOUR, 0) == HACKOS_DENIED);
        CHECK(syscall_terminal_set(HACKOS_TERMINAL_RESET, 0) == HACKOS_DENIED);
        CHECK(syscall_terminal_get(&after) == 0 && Same(initial, after));
        Print("TERMINAL_BACKGROUND_DENIED\n");
        return 0;
    }
    if (argc == 2 && Equal(argv[1], "paint"))
    {
        CHECK(syscall_console(1) == 0);
        Print("Ag\tZ\nTERMINAL_PAINT_READY\n");
        CHECK(syscall_terminal_read(0) != HACKOS_DENIED);
        return 0;
    }

    CHECK(syscall_terminal_get(nullptr) == HACKOS_ERROR);
    CHECK(syscall_terminal_get((HackosTerminalSettings*)"readonly") == HACKOS_ERROR);
    CHECK(syscall_terminal_get((HackosTerminalSettings*)0x12345) == HACKOS_ERROR);
    CHECK(syscall_terminal_get((HackosTerminalSettings*)0x4000001fffff) == HACKOS_ERROR);
    const uint64_t invalid[] = {0, 7, 9, 65, 0x100000010ULL, ~0ULL};
    for (auto size : invalid)
    {
        CHECK(syscall_terminal_set(HACKOS_TERMINAL_FONT_SIZE, size) == HACKOS_ERROR);
    }
    CHECK(syscall_terminal_set(HACKOS_TERMINAL_RESET, 1) == HACKOS_ERROR);
    CHECK(syscall_terminal_set(4, 0) == HACKOS_ERROR);
    CHECK(syscall_terminal_set(HACKOS_TERMINAL_BG_COLOUR, 0x100000000ULL) == HACKOS_ERROR);
    CHECK(syscall_terminal_get(&after) == 0 && Same(initial, after));

    const char* child[] = {"/usr/bin/TERMSET", "background"};
    auto pid = syscall_spawn_args(child[0], 2, child);
    CHECK(pid != HACKOS_ERROR);
    int64_t status = -1;
    CHECK(syscall_wait(pid, &status, 0) == pid && status == 0);

    CHECK(Command("set", "font_size", "16", 0) == 0);
    CHECK(syscall_terminal_get(&initial) == 0);
    CHECK(Command("set", "font_size", "32", 0) == 0);
    CHECK(syscall_terminal_get(&after) == 0 && after.fontSize == 32);
    CHECK(after.columns == initial.columns / 2 && after.rows == initial.rows / 2);
    HackosTerminalInfo descriptor = {};
    CHECK(syscall_terminal_info(1, &descriptor) == 0 && descriptor.columns == after.columns &&
          descriptor.rows == after.rows);
    CHECK(Command("set", "bg_col", "0x80402080", 0) == 0);
    CHECK(Command("set", "font_col", "FFFFFF80", 0) == 0);
    CHECK(syscall_terminal_get(&after) == 0 && after.bgColour == 0x80402080 && after.fontColour == 0xffffff80);
    initial = after;
    const char* badSizes[] = {"0", "7", "9", "65", "9999999999999999999999", "-16", "16px", ""};
    for (auto value : badSizes)
    {
        CHECK(Command("set", "font_size", value, 2) == 0);
    }
    const char* badColours[] = {"123", "0x123456789", "0xGG0000FF", "#ffffff", "-1", ""};
    for (auto value : badColours)
    {
        CHECK(Command("set", "bg_col", value, 2) == 0);
    }
    CHECK(Command("set", "rows", "12", 2) == 0);
    CHECK(Command("set", "unknown", "12", 2) == 0);
    CHECK(Command("get", "unknown", nullptr, 2) == 0);
    CHECK(Command("bogus", nullptr, nullptr, 2) == 0);
    CHECK(syscall_terminal_get(&after) == 0 && Same(initial, after));
    CHECK(Command("set", "font_size", "default", 0) == 0);
    CHECK(Command("set", "bg_col", "default", 0) == 0);
    CHECK(Command("set", "font_col", "default", 0) == 0);
    CHECK(syscall_terminal_get(&after) == 0 && after.fontSize == 30 && after.bgColour == 0x000000ff &&
          after.fontColour == 0xffffffff);
    CHECK(Command("set", "font_size", "8", 0) == 0);
    CHECK(Command("reset", nullptr, nullptr, 0) == 0);
    CHECK(syscall_terminal_get(&initial) == 0 && Same(initial, after));
    Print("TERMINAL_ABI_OK\n");
    return 0;
}
