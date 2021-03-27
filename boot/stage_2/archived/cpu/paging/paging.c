#include "paging.h"
#include "../../libc/stdio.h"
#include "../idt.h"

page_directory_t* kernel_directory = 0;
page_directory_t* current_directory = 0;

uint32_t* frames = 0;
uint32_t nframes = 0;

extern uint32_t free_mem_addr;
extern heap_t* kheap;

#define INDEX_FROM_BIT(a) (a / (8 * 4))
#define OFFSET_FROM_BIT(a) (a % (8 * 4))

static void set_frame(uint32_t frame_addr)
{
    uint32_t frame = frame_addr / 0x1000;
    uint32_t idx = INDEX_FROM_BIT(frame);
    uint32_t off = OFFSET_FROM_BIT(frame);

    frames[idx] |= (0x1 << off);
}

static void clear_frame(uint32_t frame_addr)
{
    uint32_t frame = frame_addr / 0x1000;
    uint32_t idx = INDEX_FROM_BIT(frame);
    uint32_t off = OFFSET_FROM_BIT(frame);

    frames[idx] &= ~(0x1 << off);
}

static uint32_t test_frame(uint32_t frame_addr)
{
    uint32_t frame = frame_addr / 0x1000;
    uint32_t idx = INDEX_FROM_BIT(frame);
    uint32_t off = OFFSET_FROM_BIT(frame);

    return (frames[idx] & (0x1 << off));
}

static uint32_t first_frame()
{
    uint32_t i, j;
    for (i = 0; i < INDEX_FROM_BIT(nframes); i++)
    {
        if (frames[i] != 0xFFFFFFFF)
        {
            for (j = 0; j < 32; j++)
            {
                uint32_t toTest = 0x1 << j;
                if (!(frames[i] & toTest))
                {
                    return i * 4 * 8 + j;
                }
            }
        }
    }
}

void alloc_frame(page_t* page, int is_kernel, int is_writeable)
{
    if (page->frame != 0)
    {
        return;
    }
    else
    {
        uint32_t idx = first_frame();
        if (idx == -1)
        {
            printf("No free frames!\n");
            asm volatile("hlt");
        }

        set_frame(idx * 0x1000);

        page->present = 1;
        page->rw = (is_writeable) ? 1 : 0;
        page->user = (is_kernel) ? 0 : 1;
        page->frame = idx;
    }
}

void free_frame(page_t* page)
{
    uint32_t frame;
    if (!(frame = page->frame))
    {
        return;
    }
    else
    {
        clear_frame(frame);
        page->frame = 0x0;
    }
}

/*
    PAGING CODE
*/

void initialise_paging()
{
    // The size of physical memory. For the moment we
    // assume it is 16MB big.
    uint32_t mem_end_page = 0x1000000;

    nframes = mem_end_page / 0x1000;
    frames = (uint32_t*) kmalloc(INDEX_FROM_BIT(nframes));
    memset((void*)frames, 0, INDEX_FROM_BIT(nframes));

    // Let's make a page directory.
    kernel_directory = (page_directory_t*) kmalloc_a(sizeof(page_directory_t));
    memset((void*)kernel_directory, 0, sizeof(page_directory_t));
    kernel_directory->physicalAddr = (uint32_t)kernel_directory->tablesPhysical;

    // Map some pages in the kernel heap area.
    // Here we call get_page but not alloc_frame. This causes page_table_t's 
    // to be created where necessary. We can't allocate frames yet because they
    // they need to be identity mapped first below, and yet we can't increase
    // placement_address between identity mapping and enabling the heap!
    int i = 0;
    for (i = KHEAP_START; i < KHEAP_START + KHEAP_INITIAL_SIZE; i += 0x1000)
    {
        get_page(i, 1, kernel_directory);
    }

    // We need to identity map (phys addr = virt addr) from
    // 0x0 to the end of used memory, so we can access this
    // transparently, as if paging wasn't enabled.
    // NOTE that we use a while loop here deliberately.
    // inside the loop body we actually change placement_address
    // by calling kmalloc(). A while loop causes this to be
    // computed on-the-fly rather than once at the start.
    // Allocate a lil' bit extra so the kernel heap can be
    // initialised properly.
    // The number 0x101000 was chosen because the kernel is supposed to be at 0x100000 
    // in memory but can't be allocated there for now, so we have to add that offset here
    // the 0x1000 offset is the one plus page for additional data of the heap
    i = 0;
    while (i < free_mem_addr + 0x1000)
    {
        // Kernel code is readable but not writeable from userspace.
        alloc_frame(get_page(i, 1, kernel_directory), 0, 0);
        i += 0x1000;
    }

    //Now let's allocate the pages from earlier
    for(i = KHEAP_START; i < KHEAP_START + KHEAP_INITIAL_SIZE; i += 0x1000)
    {
        alloc_frame(get_page(i, 1, kernel_directory), 0, 0);
    }

    register_interrupt_handler(14, page_fault);
    switch_page_directory(kernel_directory);

    //Enable paging (should be done once)
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile("mov %0, %%cr0" :: "r"(cr0));

    kheap = create_heap(KHEAP_START, KHEAP_START + KHEAP_INITIAL_SIZE, 0xCFFFF000, 0, 0);

    current_directory = clone_directory(kernel_directory);
    switch_page_directory(current_directory);
}

void switch_page_directory(page_directory_t* dir)
{
    current_directory = dir;
    asm volatile("mov %0, %%cr3" :: "r"(dir->physicalAddr));
}

page_t* get_page(uint32_t address, int make, page_directory_t* dir)
{
    address /= 0x1000;
    uint32_t table_idx = address / 1024;
    if (dir->tables[table_idx])
    {
        return &dir->tables[table_idx]->pages[address % 1024];
    }
    else if(make)
    {
        uint32_t tmp;
        dir->tables[table_idx] = (page_table_t*) kmalloc_ap(sizeof(page_table_t), &tmp);
        memset((void*)dir->tables[table_idx], 0, 0x1000);
        dir->tablesPhysical[table_idx] = tmp | 0x7;
        return &dir->tables[table_idx]->pages[address % 1024];
    }
    else
    {
        return 0;
    }
}

void page_fault(registers_t* regs)
{
    // A page fault has occurred.
    // The faulting address is stored in the CR2 register.
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

    int present = !(regs->err_code & 0x1);
    int rw = regs->err_code & 0x2;
    int us = regs->err_code & 0x4;
    int reserved = regs->err_code & 0x8;
    int id = regs->err_code & 0x10;

    // Output an error message.
    printf("Page fault (");
    if (present) printf("present (protected), "); else printf("not present, ");
    if (rw) printf("writing, "); else printf("reading, ");
    if (us) printf("user-mode, ");
    if (reserved) printf("reserved, ");
    if (id) printf("instruction fetch, ");
    printf_backspace(); printf_backspace(); //Delete the space and the last comma
    printf("), address: %x\n", faulting_address);

    printf("Dumping registers:\n");

    printf("EAX: %x, EBX: %x, ECX: %x, EDC: %x\n", regs->eax, regs->ebx, regs->ecx, regs->edx);
    printf("EFLAGS: %x, EIP: %x, ESP: %x, EBP: %x\n", regs->eflags, regs->eip, regs->esp, regs->ebp);
    printf("EDI: %x, ESI: %x, CS: %x, DS: %x\n", regs->eflags, regs->eip, regs->cs, regs->ds);

    while(1) asm volatile("hlt");
}

static page_table_t* clone_table(page_table_t* src, uint32_t* physicalAddress)
{
    page_table_t* table = (page_table_t*) kmalloc_ap(sizeof(page_table_t), physicalAddress);
    memset(table, 0, sizeof(page_table_t));

    int i;
    for (i = 0; i < 1024; i++)
    {
        if (!src->pages[i].frame)
            continue;

        alloc_frame(&table->pages[i], 0, 0);

        if (src->pages[i].present) table->pages[i].present = 1;
        if (src->pages[i].rw)      table->pages[i].rw = 1;
        if (src->pages[i].user)    table->pages[i].user = 1;
        if (src->pages[i].accessed)table->pages[i].accessed = 1;
        if (src->pages[i].dirty)   table->pages[i].dirty = 1;

        copy_page_physical(src->pages[i].frame * 0x1000, table->pages[i].frame * 0x1000);
    }

    return table;
}

page_directory_t* clone_directory(page_directory_t* src)
{
    uint32_t physical;
    page_directory_t* dir = (page_directory_t*) kmalloc_ap(sizeof(page_directory_t), &physical);
    memset(dir, 0, sizeof(page_directory_t));

    uint32_t off = (uint32_t)dir->tablesPhysical - (uint32_t)dir;
    dir->physicalAddr = physical + off;

    int i;
    for (i = 0; i < 1024; i++)
    {
        if (!src->tables[i])
            continue;

        if (kernel_directory->tables[i] == src->tables[i])
        {
           // It's in the kernel, so just use the same pointer.
           dir->tables[i] = src->tables[i];
           dir->tablesPhysical[i] = src->tablesPhysical[i];
        }
        else
        {
           uint32_t phys;
           dir->tables[i] = clone_table(src->tables[i], &phys);
           dir->tablesPhysical[i] = phys | 0x07;
        }
    }

    return dir;
}