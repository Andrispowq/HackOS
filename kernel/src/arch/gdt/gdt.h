#ifndef GDT_H
#define GDT_H

#include "lib/stdint.h"

#define GDT_ENTRIES 10

typedef struct gdt_entry_struct
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t limit_high;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct gdt_ptr_struct
{
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

extern void __gdt_flush(uint32_t);

void InstallGDT();

gdt_entry_t gdt_entries[GDT_ENTRIES];
gdt_ptr_t gdt_ptr;

#endif