#pragma once
#include "lib/stdint.h"

// This kernel currently schedules only on the bootstrap CPU.
class InterruptGuard
{
    uint64_t flags;

  public:
    InterruptGuard()
    {
        asm volatile("pushfq; popq %0; cli" : "=r"(flags)::"memory");
    }

    ~InterruptGuard()
    {
        if (flags & (1 << 9))
        {
            asm volatile("sti" ::: "memory");
        }
    }

    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;
};

void DisablePreemption();
void EnablePreemption();

class PreemptionGuard
{
  public:
    PreemptionGuard()
    {
        DisablePreemption();
    }

    ~PreemptionGuard()
    {
        EnablePreemption();
    }

    PreemptionGuard(const PreemptionGuard&) = delete;
    PreemptionGuard& operator=(const PreemptionGuard&) = delete;
};
