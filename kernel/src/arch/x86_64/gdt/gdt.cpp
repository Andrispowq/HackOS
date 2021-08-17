#include "gdt.h"

#include "lib/memory.h"
#include "tss.h"

void GDTR::SetGate(uint8_t number, uint64_t base, uint32_t limit, uint8_t access, uint8_t granularity)
{
    offset[number].base_low = (base & 0xFFFF);
    offset[number].base_middle = (base >> 16) & 0xFF;
    offset[number].base_high = (base >> 24) & 0xFF;

    offset[number].limit_low = (limit & 0xFFFF);
    offset[number].limit_high = (limit >> 16) & 0x0F;
    
    offset[number].limit_high |= granularity & 0xF0;
    offset[number].access = access;
}

void GDTR::Flush()
{
    __gdt_flush((uint64_t)this);
    __tss_flush(0x40);
}

static GDTGate gates[GDT_ENTRIES];
static TSS_Struct tss;

void InitialiseGDT()
{
    GDTR gdtr;
    gdtr.offset = gates;
    gdtr.limit = sizeof(GDTGate) * GDT_ENTRIES - 1;

    uint64_t rsp = (uint64_t)kmalloc(4096);
    tss.ist1_lo = uint32_t(rsp & 0xFFFFFFFF);
    tss.ist1_hi = uint32_t((rsp >> 32) & 0xFFFFFFFF);
    tss.rsp0_lo = uint32_t(rsp & 0xFFFFFFFF);
    tss.rsp0_hi = uint32_t((rsp >> 32) & 0xFFFFFFFF);

    gdtr.SetGate(0, 0, 0, 0, 0);                                // Null segment
    gdtr.SetGate(1, 0, 0, 0x9A, 0xA0);                          // Code segment
    gdtr.SetGate(2, 0, 0, 0x92, 0xA0);                          // Data segment
    gdtr.SetGate(3, 0, 0, 0, 0);                                // Null segment
    gdtr.SetGate(4, 0, 0, 0xF2, 0xA0);                          // User mode data segment
    gdtr.SetGate(5, 0, 0, 0xFA, 0xA0);                          // User mode code segment
    gdtr.SetGate(6, 0, 0, 0x92, 0xA0);                          // OVMF data segment
    gdtr.SetGate(7, 0, 0, 0x9A, 0xA0);                          // OVMF code segment
    gdtr.SetGate(8, (uint64_t)&tss, sizeof(tss), 0x89, 0x40);   // TSS

    gdtr.Flush();
}