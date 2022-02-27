#ifndef SYSCALL_H
#define SYSCALL_H

#include "lib/stdint.h"

extern "C" void MakeSyscall(uint64_t syscall, uint64_t p0, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4);
extern "C" int ExecuteSyscall(uint64_t p0, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4, uint64_t addr);

void InitialiseSyscalls();

#endif