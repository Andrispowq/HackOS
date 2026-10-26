#pragma once

#include "hackos/abi.h"

class Process;

uint64_t UserWindowCreate(const HackosWindowSpec* source);
uint64_t UserWindowBlit(uint64_t handle, const HackosWindowRect* source, const uint32_t* pixels);
uint64_t UserWindowPoll(uint64_t handle);
uint64_t UserWindowClose(uint64_t handle);
void ReleaseUserWindows(Process* process);

extern uint64_t user_windows_live;
