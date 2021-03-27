#include "gdt.h"

void SetGateGDT(uint8_t number, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity)
{
    gdt_entries[number].base_low = (base & 0xFFFF);
    gdt_entries[number].base_middle = (base >> 16) & 0xFF;
    gdt_entries[number].base_high = (base >> 24) & 0xFF;

    gdt_entries[number].limit_low = (limit & 0xFFFF);
    gdt_entries[number].limit_high = (limit >> 16) & 0x0F;
    
    gdt_entries[number].limit_high |= granularity & 0xF0;
    gdt_entries[number].access = access;
}

void InstallGDT()
{
    gdt_ptr.limit = sizeof(gdt_entry_t) * GDT_ENTRIES - 1;
    gdt_ptr.base  = (uint64_t)&gdt_entries;

    SetGateGDT(0, 0, 0, 0, 0);              // Null segment
    SetGateGDT(1, 0, 0, 0x9A, 0xA0);        // Code segment
    SetGateGDT(2, 0, 0, 0x92, 0xA0);        // Data segment
    SetGateGDT(3, 0, 0, 0, 0);              // Null segment
    SetGateGDT(4, 0, 0, 0xF2, 0xA0);        // User mode data segment
    SetGateGDT(5, 0, 0, 0xFA, 0xA0);        // User mode code segment
    SetGateGDT(6, 0, 0, 0x92, 0xA0);        // OVMF data segment
    SetGateGDT(7, 0, 0, 0x9A, 0xA0);        // OVMF code segment
    SetGateGDT(8, 0, 0, 0x82, 0xA0);        // TSS low
    SetGateGDT(9, 0, 0, 0, 0);              // TSS high

    __gdt_flush((uint64_t)&gdt_ptr);
}