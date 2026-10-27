#pragma once
#include "process.h"
#include "arch/x86_64/timer/pit.h"

// Relative milliseconds, bounded to uint32_t; subtraction survives tick wrap.
class WaitDeadline
{
    uint64_t timeout, end;

  public:
    explicit WaitDeadline(uint64_t milliseconds)
        : timeout(milliseconds), end(tick + TimerTicksForMilliseconds((uint32_t)milliseconds))
    {
    }

    bool Valid() const
    {
        return timeout == HACKOS_WAIT_FOREVER || timeout <= 0xffffffff;
    }

    bool Expired() const
    {
        return timeout != HACKOS_WAIT_FOREVER && (int64_t)(tick - end) >= 0;
    }

    void Block(uint64_t state) const
    {
        auto process = GetRunningProcess();
        process->waitTimed = timeout != HACKOS_WAIT_FOREVER;
        process->wakeTick = end;
        process->state = state;
        ScheduleIRQ();
        process->waitTimed = false;
    }
};
