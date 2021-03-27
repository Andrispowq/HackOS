#ifndef IDT_H
#define IDT_H

#include "lib/stdint.h"

#define KERNEL_CS 0x08

typedef struct 
{
    uint16_t offset0;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset1;
    uint32_t offset2;
    uint32_t ignore;
} __attribute__((packed)) idt_gate_t;

typedef struct 
{
    uint16_t limit;
    uint64_t offset;
} __attribute__((packed)) idt_register_t;

#define IDT_ENTRIES 256

idt_gate_t idt[IDT_ENTRIES];
idt_register_t idt_reg;

void SetIDTGate(uint8_t entry, uint8_t flags, uint64_t handler);
void SetIDT();

extern void __set_idt(idt_register_t* idt_reg);

#endif