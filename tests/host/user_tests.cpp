#include "support.hpp"
#include "../../libs/libc/src/memory.h"
#include "../../libs/libc/src/string.h"

// Explicit allocator adapter for the production split implementation.
void* malloc(size_t size)
{
    return test_alloc(size);
}

void free(void* pointer)
{
    test_free(pointer);
}

static void prepare_split_allocator()
{
}

static void release_split_allocation(void* p)
{
    free(p);
}

#define LIBRARY_CASE(name) TEST(user_##name)
#include "memory_string_cases.hpp"
#include "split_cases.hpp"
#undef LIBRARY_CASE
