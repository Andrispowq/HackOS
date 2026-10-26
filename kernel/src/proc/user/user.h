#pragma once
#include "arch/x86_64/interrupts/idt.h"

bool LoadUserProgramTask(const char* path);
[[noreturn]] void ExitUserProcess(uint64_t code);
[[noreturn]] void FaultUserProcess(Registers* frame);
uint64_t user_write(const char* buffer, uint64_t length);
