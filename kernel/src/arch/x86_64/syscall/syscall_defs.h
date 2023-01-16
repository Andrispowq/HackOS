#ifndef SYSCALL_DEFS_H
#define SYSCALL_DEFS_H

#include "proc/tasking/process.h"

extern char system_input[256];

void exit(int code);
char* kread();

uint64_t create_window(uint32_t width, uint32_t height, uint32_t flags);
void destroy_window(uint64_t ID);

#endif