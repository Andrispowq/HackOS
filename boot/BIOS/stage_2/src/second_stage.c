
#include "cpu/idt.h"
#include "cpu/isr.h"
#include "cpu/paging/paging.h"
#include "cpu/rsdp.h"
#include "cpu/memory_map.h"

#include "drivers/rtc.h"
#include "drivers/screen.h"

#include "libc/font/font.h"
#include "libc/memory.h"
#include "libc/stdio.h"
#include "libc/function.h"

#include "elf_loader/elf.h"

struct tm start_time;

struct KernelInfo
{
    FRAMEBUFFER framebuffer;
    PSF1_FONT* font;
    MemoryMapEntry* memMap;
    uint64_t memMapEntrySize;
    void* rsdp;
    uint8_t memMapEntries;
    uint8_t rsdp_version;
    uint8_t booted_from_BIOS;
};

extern uint64_t end;
uint64_t free_mem_addr;

void loader_main(struct FramebufferInfo* info)
{
    free_mem_addr = (uint64_t)&end;

    //Initialise the Interrupt Descriptor Table, and paging
    MemoryMapEntry* entries = GetMemoryRegions();

    InstallISR();
    InitPaging(entries);

    for(uint8_t i = 0; i < MemoryRegionCount; i++)
    {
        MemoryMapEntry* entry = (MemoryMapEntry*)((uint64_t)entries + i * sizeof(MemoryMapEntry));
    
        for(uint64_t i = entry->address; i < (entry->address + entry->length + 0x1000); i += 0x1000)
        {
            MapMemory(i, i);
        }
    }
    
    //Get the RSDP
    uint8_t version;
    RSDP1* rsdp = FindRSDP(&version);

    //Load in the font file, loaded at 0x6000
    PSF1_FONT* font = LoadFont((void*)0x6000);
    
    //Initialise the display
    init_display(info, font);
    clrscr();

    rtc_init(&start_time);
    kprintf("Welcome to the HackOS!\n");
    kprintf("[%x:%x:%x]: Starting the initialisation!\n", 
        start_time.hour, start_time.minute, start_time.second);

    //Read the kernel, which is 128 sectors long (now), and starts at the 130th sector
    uint64_t kernelMemory;
    Elf64_Ehdr* header = LoadProgram((void*)0x20000, &kernelMemory);
    if(!header)
    {
        kprintf("ELF header not supported!\n");
        while(1)
        {
            asm("hlt");
        }
    }

    PrepareProgram(header, kernelMemory);

    struct KernelInfo kernelInfo;
    kernelInfo.framebuffer = get_current_display()->framebuffer;
    kernelInfo.font = font;
    kernelInfo.memMap = entries;
    kernelInfo.rsdp = rsdp;
    kernelInfo.memMapEntrySize = sizeof(MemoryMapEntry);
    kernelInfo.memMapEntries = MemoryRegionCount;
    kernelInfo.rsdp_version = version;
    kernelInfo.booted_from_BIOS = 1;

    int(*kernel_main)(struct KernelInfo*) = (int(*)(struct KernelInfo*)) header->e_entry;
    int ret = kernel_main(&kernelInfo);

    kprintf("Kernel returned with code %d!\n", ret);

    //We don't want to exit the second stage bootloader, so we put an infinite loop (for now)
    while(1)
    {
        asm("hlt");
    }
}