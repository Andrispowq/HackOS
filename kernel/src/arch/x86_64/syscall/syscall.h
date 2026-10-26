#ifndef SYSCALL_H
#define SYSCALL_H

#include "fs/vfs.h"

#include "lib/stdint.h"
#include "lib/string.h"
#include "lib/memory.h"

#include "syscall_defs.h"

#include <stdarg.h>

#include "hackos/syscall.h"
extern "C" uint64_t ExecuteSyscall(uint64_t p0, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4, uint64_t addr);

void InitialiseSyscalls();

#endif
