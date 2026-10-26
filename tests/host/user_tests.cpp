#include "support.hpp"
#include "../../libs/libc/src/memory.h"
#include "../../libs/libc/src/string.h"
// Syscall allocation is a seam; no userspace transition is exercised.
uint64_t syscall_kmalloc_int(uint64_t size,int,uint64_t*) { return (uint64_t)test_alloc(size); }
uint64_t syscall_kfree(void* pointer) { test_free(pointer); return 0; }
static void prepare_split_allocator() {  }
static void release_split_allocation(void* p) { free(p); }
#define LIBRARY_CASE(name) TEST(user_##name)
#include "memory_string_cases.hpp"
#include "split_cases.hpp"
#undef LIBRARY_CASE
