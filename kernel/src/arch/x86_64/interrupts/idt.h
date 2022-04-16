#ifndef IDT_H
#define IDT_H

#include "lib/stdint.h"

#define KERNEL_CS 0x08
#define IDT_ENTRIES 256

#define IDT_TA_InterruptGate    0b10001110
#define IDT_TA_CallGate         0b10001100
#define IDT_TA_TrapGate         0b10001111

#define IRQ0 32
#define IRQ1 33
#define IRQ2 34
#define IRQ3 35
#define IRQ4 36
#define IRQ5 37
#define IRQ6 38
#define IRQ7 39
#define IRQ8 40
#define IRQ9 41
#define IRQ10 42
#define IRQ11 43
#define IRQ12 44
#define IRQ13 45
#define IRQ14 46
#define IRQ15 47

#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI 0x20

#define ICW1_INIT 0x10
#define ICW1_ICW4 0x01
#define ICW4_8086 0x01

struct xmm_reg
{
    uint64_t low;
    uint64_t high;
};

struct Registers
{
    uint64_t ds;
    xmm_reg xmm[16];
    uint64_t rdi, rsi, rbp, useless, rbx, rdx, rcx, rax;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

struct IDTGate
{
    uint16_t offset0;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset1;
    uint32_t offset2;
    uint32_t ignore;
} __attribute__((packed));

class IDTR
{
public:
    void SetGate(uint8_t entry, uint8_t flags, uint64_t handler);
    void Flush();

    uint16_t limit;
    IDTGate* offset;
} __attribute__((packed));

void InitialiseIDT();
void InitialiseIRQ();

extern "C"
{
    void ISRHandler(Registers* regs);
    void IRQHandler(Registers* regs);
}

typedef void (*isr_t)(Registers*);
void RegisterInterruptHandler(uint8_t index, isr_t handler);

extern "C" void __set_idt(IDTR* idtr);

//ISR handlers
extern "C"
{

    /* ISR definitions */
    extern void isr0();
    extern void isr1();
    extern void isr2();
    extern void isr3();
    extern void isr4();
    extern void isr5();
    extern void isr6();
    extern void isr7();
    extern void isr8();
    extern void isr9();
    extern void isr10();
    extern void isr11();
    extern void isr12();
    extern void isr13();
    extern void isr14();
    extern void isr15();
    extern void isr16();
    extern void isr17();
    extern void isr18();
    extern void isr19();
    extern void isr20();
    extern void isr21();
    extern void isr22();
    extern void isr23();
    extern void isr24();
    extern void isr25();
    extern void isr26();
    extern void isr27();
    extern void isr28();
    extern void isr29();
    extern void isr30();
    extern void isr31();

    /* IRQ definitions */
    extern void irq0();
    extern void irq1();
    extern void irq2();
    extern void irq3();
    extern void irq4();
    extern void irq5();
    extern void irq6();
    extern void irq7();
    extern void irq8();
    extern void irq9();
    extern void irq10();
    extern void irq11();
    extern void irq12();
    extern void irq13();
    extern void irq14();
    extern void irq15();

    /* syscall handler */
    extern void isr128();
}

#endif