#include "support.hpp"
#include "hackos/abi.h"
#include "hackos/syscall_numbers.h"
static_assert(HACKOS_SYS_exit == 0 && HACKOS_SYS_user_write == 14 &&
              HACKOS_SYS_input == 15 && HACKOS_SYS_power == 26 &&
              HACKOS_SYS_yield == 27 && HACKOS_SYS_spawn_args == 28 && HACKOS_SYS_wait == 29 && HACKOS_SYS_spawn_foreground == 30 && HACKOS_SYS_window_create == 31 && HACKOS_SYS_window_close == 34 && HACKOS_SYS_window_state == 35 && HACKOS_SYS_window_event == 36 && HACKOS_SYSCALL_COUNT == 37, "User syscall ABI changed");
TEST(syscall_record_layout)
{
    CHECK(sizeof(HackosClock) == 16);
    CHECK(sizeof(HackosWindowSpec) == 48);
    CHECK(sizeof(HackosWindowRect) == 16);
    CHECK(sizeof(HackosWindowState) == 32);
    CHECK(sizeof(HackosWindowEvent) == 32);
    CHECK(__builtin_offsetof(HackosWindowSpec, title) == 16);
    CHECK(sizeof(HackosFileInfo) == 272 && sizeof(HackosProcessInfo) == 272);
    CHECK(__builtin_offsetof(HackosFileInfo, name) == 16);
    CHECK(__builtin_offsetof(HackosProcessInfo, name) == 16);
    return 0;
}
