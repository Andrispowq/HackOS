#include "idt.h"

void SetIDTGate(uint8_t entry, uint8_t flags, uint64_t handler) 
{
    idt[entry].offset0 = (uint16_t)(handler & 0x000000000000FFFF);
    idt[entry].offset1 = (uint16_t)((handler & 0x00000000FFFF0000) >> 16);
    idt[entry].offset2 = (uint32_t)((handler & 0xFFFFFFFF00000000) >> 32);

    idt[entry].selector = KERNEL_CS;
    idt[entry].flags = flags;
}

void SetIDT() 
{
    idt_reg.offset = (uint64_t) &idt;
    idt_reg.limit = IDT_ENTRIES * sizeof(idt_gate_t) - 1;

    __set_idt(&idt_reg);
}