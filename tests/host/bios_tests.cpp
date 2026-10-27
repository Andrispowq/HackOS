#include "support.hpp"
extern "C"
{
#include "../../boot/BIOS/stage_2/src/libc/memory.h"
#include "../../boot/BIOS/stage_2/src/libc/string.h"
    uint64_t free_mem_addr = 0x1000;
}

static void prepare_split_allocator()
{
    static char storage[4096];
    free_mem_addr = (uint64_t)storage;
}

static void release_split_allocation(void* p)
{
    (void)p;
}

#define LIBRARY_CASE(name) TEST(bios_##name)
#include "memory_string_cases.hpp"
#include "split_cases.hpp"
#undef LIBRARY_CASE
