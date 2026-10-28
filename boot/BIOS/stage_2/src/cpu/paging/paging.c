#include "paging.h"
#include "elf_loader/elf.h"
#include "libc/memory.h"

/* Six low-memory pages map the supported 32-bit physical address space.
 * Keeping bootstrap tables below 1 MiB also protects them from the kernel's
 * early bump allocator until the kernel installs its own page tables.
 * ELF validation excludes all low-memory loader/handoff/buffer ranges.
 */
int InitPaging(void)
{
    const uint64_t base = 0x40000;
    if (!BootMemoryUsable(base, 6 * 4096))
    {
        return 0;
    }
    uint64_t* tables = (uint64_t*)base;
    memset(tables, 0, 6 * 4096);
    tables[0] = (base + 4096) | 3;
    for (unsigned directory = 0; directory < 4; ++directory)
    {
        tables[512 + directory] = (base + (directory + 2) * 4096) | 3;
        for (unsigned page = 0; page < 512; ++page)
        {
            uint64_t address = ((uint64_t)directory * 512 + page) * 0x200000;
            tables[(directory + 2) * 512 + page] = address | 0x83;
        }
    }

    asm volatile("mov %0, %%cr3" : : "r"(base) : "memory");
    return 1;
}
