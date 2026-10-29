#include "support.hpp"
#include "hackos/runtime/event_queue.h"
#include "hackos/abi.h"

TEST(window_event_queue_fifo_wrap_and_overflow)
{
    EventQueue<HackosWindowEvent, 64> queue;
    HackosWindowEvent event = {};
    CHECK(!queue.Pop(event));

    for (unsigned pass = 0; pass < 3; pass++)
    {
        for (unsigned i = 0; i < 64; i++)
        {
            event = {HACKOS_WINDOW_KEY, i, -4, 5, 200, 170, 0, 1};
            queue.Push(event);
        }

        for (unsigned i = 0; i < 64; i++)
        {
            CHECK(queue.Pop(event));
            CHECK(event.key == i && event.x == -4 && event.focused == 1);
        }

        CHECK(!queue.Pop(event));
    }

    for (unsigned i = 0; i < 65; i++)
    {
        queue.Push(event);
    }

    CHECK(queue.TakeOverflow());
    CHECK(!queue.TakeOverflow());
    CHECK(!queue.Pop(event));
    queue.Push(event);
    CHECK(queue.Pop(event));
    queue.Push(event);
    CHECK(queue.Count() == 1);
    queue.Clear();
    CHECK(queue.Count() == 0);
    CHECK(!queue.Pop(event));
    queue.Overflow();
    CHECK(queue.TakeOverflow());
    return 0;
}

TEST(window_event_queue_sustained_model)
{
    EventQueue<unsigned, 64> queue;
    unsigned model[64] = {}, count = 0;
    bool overflow = false;
    uint32_t random = 0x6c123456;
    for (unsigned step = 0; step < 100000; ++step)
    {
        random = random * 1664525 + 1013904223;
        unsigned action = (random >> 24) % 16;
        if (action < 10)
        {
            bool accepted = count < 64;
            CHECK(queue.Push(step) == accepted);
            if (accepted)
            {
                model[count++] = step;
            }
            else
            {
                overflow = true;
            }
        }
        else if (action < 14)
        {
            unsigned value = 0;
            CHECK(queue.Pop(value) == (count != 0));
            if (count)
            {
                CHECK(value == model[0]);
                --count;
                for (unsigned i = 0; i < count; ++i)
                {
                    model[i] = model[i + 1];
                }
            }
        }
        else if (action == 14)
        {
            CHECK(queue.TakeOverflow() == overflow);
            if (overflow)
            {
                count = 0;
                overflow = false;
            }
        }
        else
        {
            queue.Overflow();
            overflow = true;
        }
        CHECK(queue.Count() == count && queue.Ready() == (count || overflow));
    }
    queue.Clear();
    CHECK(!queue.Ready() && !queue.TakeOverflow());
    return 0;
}
