#include "support.hpp"
#include "lib/memory.h"
#include "arch/x86_64/paging/page_table_manager.h"
#include "arch/x86_64/timer/rtc.h"
#include "proc/tasking/process.h"

// Test seams: no real heap, paging, RTC, console, or scheduler is exercised here.
bool fromUEFI = true;
uint64_t end = 0;
PageTableManager KernelDirectory(nullptr);
unsigned test_mapping_count = 0;

void* alloc(uint64_t size, uint64_t)
{
    return test_alloc(size);
}

void free(void* ptr)
{
    test_free(ptr);
}

paddr_t PageTableManager::PhysicalAddress(vaddr_t addr)
{
    return addr & ~0xfffUL;
}

void PageTableManager::MapMemory(vaddr_t, paddr_t, PageTableFlagBits)
{
    ++test_mapping_count;
}

void kprintf(const char*, ...)
{
}

tm test_rtc_time = {124, 0, 2, 3, 4, 6};

void GetTimeRTC(struct tm* value)
{
    *value = test_rtc_time;
}

Process::Process(const char*, void*, void*)
{
    test_abort("scheduler invoked in host test");
}

uint64_t AddProcess(Process*)
{
    test_abort("scheduler invoked in host test");
    return 0;
}

[[noreturn]] void _Kill()
{
    test_abort("scheduler invoked in host test");
    __builtin_unreachable();
}

void DisablePreemption()
{
}

void EnablePreemption()
{
}

bool IsRunning(uint64_t)
{
    return false;
}
