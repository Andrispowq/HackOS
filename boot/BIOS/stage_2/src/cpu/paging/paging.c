#include "paging.h"

#include "libc/memory.h"
#include "libc/string.h"
#include "libc/stdio.h"

uint64_t freeMemory;
uint64_t reservedMemory;
uint64_t usedMemory;

uint64_t* bitmap = 0;
uint64_t frameCount = 0;

page_map_level_4_t* kernel_directory;

#define INDEX_FROM_BIT(a) (a / (8 * 8))
#define OFFSET_FROM_BIT(a) (a % (8 * 8))

static void set_frame(uint64_t frame_addr)
{
    uint64_t frame = frame_addr / 0x1000;
    uint64_t idx = INDEX_FROM_BIT(frame);
    uint64_t off = OFFSET_FROM_BIT(frame);

    bitmap[idx] |= (0x1 << off);
}

static void clear_frame(uint64_t frame_addr)
{
    uint64_t frame = frame_addr / 0x1000;
    uint64_t idx = INDEX_FROM_BIT(frame);
    uint64_t off = OFFSET_FROM_BIT(frame);

    bitmap[idx] &= ~(0x1 << off);
}

static uint32_t test_frame(uint64_t frame_addr)
{
    uint64_t frame = frame_addr / 0x1000;
    uint64_t idx = INDEX_FROM_BIT(frame);
    uint64_t off = OFFSET_FROM_BIT(frame);

    return (bitmap[idx] & (0x1 << off));
}

static uint64_t first_frame()
{
    uint64_t i, j;
    for (i = 0; i < (uint64_t)frameCount; i++)
    {
        if (bitmap[i] != 0xFFFFFFFFFFFFFFFF)
        {
            for (j = 0; j < 64; j++)
            {
                uint64_t toTest = 0x1 << j;
                if (!(bitmap[i] & toTest))
                {
                    return i * 8 * 8 + j;
                }
            }
        }
    }

    return frameCount * 64;
}

void LockPages(vaddr_t address, uint64_t pageCount)
{
    for(uint32_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

        set_frame(addr);
        freeMemory -= 0x1000;
        usedMemory += 0x1000;
    }
}

void UnlockPages(vaddr_t address, uint64_t pageCount)
{
    for(uint32_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

        clear_frame(addr);
        freeMemory += 0x1000;
        usedMemory -= 0x1000;
    }
}

void ReservePages(vaddr_t address, uint64_t pageCount)
{
    for(uint32_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

        set_frame(addr);
        freeMemory -= 0x1000;
        reservedMemory += 0x1000;
    }
}

void UnreservePages(vaddr_t address, uint64_t pageCount)
{
    for(uint32_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

        clear_frame(addr);
        freeMemory += 0x1000;
        reservedMemory -= 0x1000;
    }
}

void* RequestPage()
{
    uint64_t index = first_frame();

    if(index == frameCount * 64)
    {
        while(1) asm("hlt");
    }

    LockPages(index * 0x1000, 1);
    return (void*)(index * 0x1000);
}

void InitPaging(MemoryMapEntry* entries)
{
    uint64_t memorySize = 0x10000000; //Assume this big memory
    freeMemory = memorySize;
    frameCount = memorySize / 0x1000;
    frameCount /= 64;
    frameCount += 1;

    bitmap = (uint64_t*)kmalloc(frameCount);
    memset(bitmap, 0, frameCount * sizeof(uint64_t));

    ReservePages(0x0, 0x50);

    kernel_directory = RequestPage();
    memset(kernel_directory, 0, 0x1000);

    for(uint64_t i = 0; i < 0x1000000; i += 0x1000)
    {
        MapMemory(i, i);
    }

    SetIDTGate(IRQ15, IDT_TA_InterruptGate, (uint64_t)PageFaultHandler);

    asm volatile("mov %0, %%cr3" : : "r"(kernel_directory));
}

void MapMemory(vaddr_t virtualAddress, paddr_t physicalAddress)
{
    page_map_index_t indices = IndexOf(virtualAddress);

    page_map_level_4_entry_t pml4_entry = kernel_directory->entries[indices.index_PDP];
    page_directory_pointer_t* pdp;
    if(!pml4_entry.present)
    {
        pdp = (page_directory_pointer_t*)RequestPage();
        memset(pdp, 0, 0x1000);
        pml4_entry.address = (uint64_t)pdp >> 12;
        pml4_entry.present = 1;
        pml4_entry.writeable = 1;
        kernel_directory->entries[indices.index_PDP] = pml4_entry;
    }
    else
    {
        pdp = (page_directory_pointer_t*)(uint64_t)(pml4_entry.address << 12);
    }

    page_directory_pointer_entry_t pdp_entry = pdp->entries[indices.index_PD];
    page_directory_t* pd;
    if(!pdp_entry.present)
    {
        pd = (page_directory_t*)RequestPage();
        memset(pd, 0, 0x1000);
        pdp_entry.address = (uint64_t)pd >> 12;
        pdp_entry.present = 1;
        pdp_entry.writeable = 1;
        pdp->entries[indices.index_PD] = pdp_entry;
    }
    else
    {
        pd = (page_directory_t*)(uint64_t)(pdp_entry.address << 12);
    }

    page_directory_entry_t pd_entry = pd->entries[indices.index_PT];
    page_table_t* pt;
    if(!pd_entry.present)
    {
        pt = (page_table_t*)RequestPage();
        memset(pt, 0, 0x1000);
        pd_entry.address = (uint64_t)pt >> 12;
        pd_entry.present = 1;
        pd_entry.writeable = 1;
        pd->entries[indices.index_PT] = pd_entry;
    }
    else
    {
        pt = (page_table_t*)(uint64_t)(pd_entry.address << 12);
    }

    page_table_entry_t pt_entry = pt->entries[indices.index_P];
    pt_entry.address = physicalAddress >> 12;
    pt_entry.present = 1;
    pt_entry.writeable = 1;
    pt->entries[indices.index_P] = pt_entry;
}

uint64_t GetUsedMemory()
{
    return usedMemory;
}

uint64_t GetFreeMemory()
{
    return freeMemory;
}

uint64_t GetReservedMemory()
{
    return reservedMemory;
}

page_map_index_t IndexOf(vaddr_t address)
{
    page_map_index_t ind;

    address >>= 12;
    ind.index_P = address & 0x1ff;
    address >>= 9;
    ind.index_PT = address & 0x1ff;
    address >>= 9;
    ind.index_PD = address & 0x1ff;
    address >>= 9;
    ind.index_PDP = address & 0x1ff;

    return ind;
}

void PageFaultHandler(registers_t* regs)
{
    uint64_t cr2;
    asm volatile("mov %%cr2, %0" : "=r" (cr2));

    uint32_t flags = regs->err_code;

    int present = !(flags & 0x1);
    int rw = flags & 0x2;
    int us = flags & 0x4;
    int reserved = flags & 0x8;
    int inst = flags & 0x10;

    kprintf("\n\n\n---------------------------------------------");

    kprintf("\n\n\nPAGE FAULT: at virtual address %x\n", cr2, flags);

    kprintf("Flags:\n");
    kprintf("Present: %d\n", present);
    kprintf("Write: %d\n", rw);
    kprintf("User access: %d\n", us);
    kprintf("Reserved: %d\n", reserved);
    kprintf("Instruction fetch: %d\n", inst);

    kprintf("\n\n\n---------------------------------------------\n\n\n");

    while(1) asm("hlt");
}