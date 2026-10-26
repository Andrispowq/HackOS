#include "support.hpp"
extern "C" {
#include "../../boot/BIOS/stage_2/src/libc/memory.h"
#include "../../boot/BIOS/stage_2/src/libc/string.h"
uint64_t free_mem_addr=0x1000;
}
#define LIBRARY_CASE(name) TEST(bios_##name)
#include "memory_string_cases.hpp"
#undef LIBRARY_CASE
