#include "../../../../include/hackos/serial.h"

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

static int serialAvailable;

static void SerialLog(const char* text)
{
    while (serialAvailable && *text)
    {
        serialAvailable = HackosSerialPut(*text++);
    }
}

struct tm start_time;

extern uint64_t end;
uint64_t free_mem_addr;

void loader_main(struct FramebufferInfo* info)
{
    serialAvailable = HackosSerialInitialise();
    SerialLog("HackOS BIOS loader\n");
    free_mem_addr = (uint64_t)&end;

    // Validate the disk image and RAM before mapping segment destinations.
    MemoryMapEntry* entries = GetMemoryRegions();

    if (!InspectKernel())
    {
        SerialLog("BIOS: invalid FAT32 KERNEL.ELF or destination memory\n");
        for (;;)
        {
            asm("cli; hlt");
        }
    }

    if (!InitPaging())
    {
        SerialLog("BIOS: no safe low memory for page tables\n");
        for (;;)
        {
            asm("cli; hlt");
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
    kprintf("[%x:%x:%x]: Starting the initialisation!\n", start_time.hour, start_time.minute, start_time.second);

    uint64_t entry = LoadKernel();
    if (!entry)
    {
        SerialLog("BIOS: kernel disk read failed\n");
        for (;;)
        {
            asm("cli; hlt");
        }
    }

    // BIOS INT 13h still needs firmware PIC vectors until the final read.
    InstallISR();

    struct KernelInfo kernelInfo = {0};
    kernelInfo.framebuffer = get_current_display()->framebuffer;
    kernelInfo.font = font;
    kernelInfo.memMap = entries;
    kernelInfo.rsdp = rsdp;
    kernelInfo.memMapEntrySize = sizeof(MemoryMapEntry);
    kernelInfo.memMapEntries = MemoryRegionCount;
    kernelInfo.rsdp_version = rsdp ? rsdp->revision : 0;
    kernelInfo.booted_from_BIOS = 1;

    int (*kernel_main)(struct KernelInfo*) = (int (*)(struct KernelInfo*))entry;
    int ret = kernel_main(&kernelInfo);

    kprintf("Kernel returned with code %d!\n", ret);

    //We don't want to exit the second stage bootloader, so we put an infinite loop (for now)
    while (1)
    {
        asm("hlt");
    }
}
