#include "support.hpp"
#include "hackos/runtime/split.h"

static int calls, fail_at, live;
static void* allocate_split(size_t size)
{
    if (calls++ == fail_at) return nullptr;
    void* p = test_alloc(size);
    if (p) ++live;
    return p;
}
static void release_split(void* p) { --live; test_free(p); }

TEST(split_allocation_failure_cleanup)
{
    // One pointer array plus four strings: fail each allocation in turn.
    for (fail_at = 0; fail_at < 5; ++fail_at)
    {
        calls = live = 0;
        int count = 99;
        CHECK(!hackos_split("a,,b,", ',', &count, allocate_split, release_split));
        CHECK(count == 0 && live == 0 && calls == fail_at + 1);
    }
    return 0;
}
