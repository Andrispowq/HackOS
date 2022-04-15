#ifndef SYSCALL_DEFS_H
#define SYSCALL_DEFS_H

#include "proc/tasking/process.h"

extern char system_input[256];

void exit(int code);
char* kread();

#endif