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
