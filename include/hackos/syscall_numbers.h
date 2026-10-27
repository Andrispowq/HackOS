#pragma once

enum HackosSyscallNumber
{

#define HACKOS_SYSCALL(number, name, target, arity, ...) HACKOS_SYS_##name = number,
#include "hackos/syscalls.def"
#undef HACKOS_SYSCALL
    HACKOS_SYSCALL_COUNT
};
