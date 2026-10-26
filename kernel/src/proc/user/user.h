#pragma once
#include "arch/x86_64/interrupts/idt.h"

uint64_t LoadUserProgramTask(const char* path, bool systemShell = false);
[[noreturn]] void ExitUserProcess(uint64_t code);
[[noreturn]] void FaultUserProcess(Registers* frame);
uint64_t user_write(const char* buffer, uint64_t length);
