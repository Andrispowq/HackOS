#include "elf.h"
#include <hackos/boot_elf.h>
#include "../fat32/boot_fat.h"
#include "libc/memory.h"
#include "cpu/memory_map.h"

static Elf64_Ehdr header;
static Elf64_Phdr segments[HACKOS_BOOT_PHNUM];
extern int BiosReadSector(uint32_t lba);

static int DiskRead(uint32_t lba, void* output)
{
    if (!BiosReadSector(lba))
    {
        return 0;
    }
    memcpy(output, (void*)0x20000, 512);
    return 1;
}

int BootMemoryUsable(uint64_t start, uint64_t size)
{
    MemoryMapEntry* entries = GetMemoryRegions();
    uint64_t end = start + size;
    int found = 0;
    for (unsigned i = 0; i < MemoryRegionCount; ++i)
    {
        MemoryMapEntry* e = entries + i;
        if (e->size > ~0ULL - e->address)
        {
            return 0;
        }
        uint64_t last = e->address + e->size;
        if (e->type != 1 && start < last && e->address < end)
        {
            return 0;
        }
        if (e->type == 1 && start >= e->address && end <= last)
        {
            found = 1;
        }
    }

    return found;
}

int InspectKernel(void)
{
    extern uint64_t end;
    if (!BootMemoryUsable(0x8000, (uint64_t)&end + 8192 - 0x8000) || !BootMemoryUsable(0x20000, 512) ||
        !BootMemoryUsable(0x30000, 255 * 24))
    {
        return 0;
    }
    if (!BootFatOpen(DiskRead) || !BootFatRead(0, &header, sizeof(header)) ||
        !HackosBootElfHeader(&header, BootFatSize()) ||
        !BootFatRead(header.e_phoff, segments, header.e_phnum * sizeof(Elf64_Phdr)) ||
        !HackosBootElfSegments(&header, segments, BootFatSize()))
    {
        return 0;
    }

    for (unsigned i = 0; i < header.e_phnum; ++i)
    {
        Elf64_Phdr* p = segments + i;
        if (p->p_type == PT_LOAD && p->p_memsz && !BootMemoryUsable(p->p_paddr, (p->p_memsz + 4095) & ~4095ULL))
        {
            return 0;
        }
    }

    return 1;
}

uint64_t LoadKernel(void)
{
    for (unsigned i = 0; i < header.e_phnum; ++i)
    {
        Elf64_Phdr* p = segments + i;
        if (p->p_type != PT_LOAD || !p->p_memsz)
        {
            continue;
        }
        if (!BootFatRead(p->p_offset, (void*)p->p_paddr, p->p_filesz))
        {
            return 0;
        }
        memset((void*)(p->p_paddr + p->p_filesz), 0, p->p_memsz - p->p_filesz);
    }

    return header.e_entry;
}
