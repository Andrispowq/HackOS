#pragma once
#include "hackos/hardware.h"

// Caller must hold the scheduler's preemption guard. No hardware writes,
// allocation, console output, blocking, or user pointers in this query path.
uint64_t ReadHardware(uint64_t section, uint64_t index, HackosHardwareInfo& info);
