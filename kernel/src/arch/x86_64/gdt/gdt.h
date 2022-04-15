#ifndef GDT_H
#define GDT_H

#include "lib/stdint.h"

#define GDT_ENTRIES 8

struct GDTGate
{
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t limit_high;
    uint8_t base_high;
} __attribute__((packed));

class GDTR
{
public:
    void SetGate(uint8_t gate, uint64_t base, uint32_t limit, uint8_t access, uint8_t granularity);
    void Flush();

    uint16_t limit;
    GDTGate* offset;
} __attribute__((packed));

extern "C" void __gdt_flush(uint64_t gdtr);
extern "C" void __tss_flush(uint64_t tss);

void InitialiseGDT(bool fromUEFI);

#endif