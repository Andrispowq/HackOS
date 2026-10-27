#pragma once

// Callers serialize access. Overflow discards the incomplete sequence so the
// consumer can resynchronize from an authoritative state snapshot.
template <typename T, unsigned Capacity> class EventQueue
{
  public:
    bool Push(const T& event)
    {
        if (count == Capacity)
        {
            overflow = true;
            return false;
        }

        events[(read + count) % Capacity] = event;
        count++;
        return true;
    }

    bool Pop(T& event)
    {
        if (!count)
        {
            return false;
        }

        event = events[read];
        read = (read + 1) % Capacity;
        count--;
        return true;
    }

    unsigned Count() const
    {
        return count;
    }

    void Clear()
    {
        read = 0;
        count = 0;
        overflow = false;
    }

    void Overflow()
    {
        overflow = true;
    }

    bool Ready() const
    {
        return count || overflow;
    }

    bool TakeOverflow()
    {
        if (!overflow)
        {
            return false;
        }

        overflow = false;
        read = 0;
        count = 0;
        return true;
    }

  private:
    T events[Capacity] = {};
    unsigned read = 0;
    unsigned count = 0;
    bool overflow = false;
};
