// Test the real contiguous allocator with an explicit synthetic ownership map.
#define private public
#include "arch/x86_64/paging/page_frame_allocator.h"
#undef private

#define REQUIRE(condition)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            return __LINE__;                                                                                           \
        }                                                                                                              \
    } while (false)

int main()
{
    uint64_t bits[8];
    for (auto& word : bits)
    {
        word = ~0ULL;
    }
    PageFrameAllocator allocator;
    allocator.bitmap.SetBuffer(bits);
    allocator.bitmap.SetSize(sizeof(bits));
    allocator.freeMemory = allocator.usedMemory = allocator.reservedMemory = 0;
    for (unsigned i = 260; i < 268; ++i)
    {
        allocator.bitmap.Set(i, false);
        allocator.freeMemory += 4096;
    }
    REQUIRE(!allocator.TryRequestSurfacePages(0));
    REQUIRE(!allocator.TryRequestSurfacePages(16385));
    REQUIRE(!allocator.TryRequestSurfacePages(9));
    REQUIRE(allocator.freeMemory == 8 * 4096);
    void* first = allocator.TryRequestSurfacePages(4);
    REQUIRE(first == (void*)(260 * 4096));
    REQUIRE(allocator.usedMemory == 4 * 4096);
    void* second = allocator.TryRequestSurfacePages(4);
    REQUIRE(second == (void*)(264 * 4096));
    REQUIRE(!allocator.TryRequestSurfacePages(1));
    allocator.FreePages(first, 4);
    allocator.FreePages(second, 4);
    REQUIRE(allocator.freeMemory == 8 * 4096 && allocator.usedMemory == 0);
    // Total free space is sufficient, but no run can satisfy this request.
    allocator.LockPages((void*)(263 * 4096), 1);
    REQUIRE(!allocator.TryRequestSurfacePages(5));
    REQUIRE(allocator.freeMemory == 7 * 4096 && allocator.usedMemory == 4096);
    REQUIRE(allocator.TryRequestSurfacePages(4) == (void*)(264 * 4096));
    return 0;
}
