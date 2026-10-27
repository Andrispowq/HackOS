#pragma once
#include "arch/x86_64/interrupts/idt.h"
#include "hackos/abi.h"

uint64_t LoadUserProgramTask(const char* path, bool systemShell = false, uint64_t argc = 0,
                             const char* const* argv = nullptr, const HackosCredentials* credentials = nullptr);
[[noreturn]] void ExitUserProcess(uint64_t code);
[[noreturn]] void FaultUserProcess(Registers* frame);
uint64_t user_write(const char* buffer, uint64_t length);
