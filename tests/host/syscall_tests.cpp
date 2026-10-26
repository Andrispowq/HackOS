#include "support.hpp"
#include "hackos/abi.h"
#include "hackos/syscall_numbers.h"
static_assert(HACKOS_SYS_exit == 0 && HACKOS_SYS_user_write == 14 &&
              HACKOS_SYS_input == 15 && HACKOS_SYS_power == 26 &&
              HACKOS_SYS_yield == 27 && HACKOS_SYS_spawn_args == 28 && HACKOS_SYS_wait == 29 && HACKOS_SYSCALL_COUNT == 30, "User syscall ABI changed");
TEST(syscall_record_layout)
{
    CHECK(sizeof(HackosClock) == 16);
    CHECK(sizeof(HackosFileInfo) == 272 && sizeof(HackosProcessInfo) == 272);
    CHECK(__builtin_offsetof(HackosFileInfo, name) == 16);
    CHECK(__builtin_offsetof(HackosProcessInfo, name) == 16);
    return 0;
}
