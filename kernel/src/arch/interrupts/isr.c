#include "isr.h"

#include "idt.h"
#include "arch/ports.h"

#include "lib/stdio.h"

#include "arch/timer/pit.h"
#include "drivers/keyboard/keyboard.h"
#include "drivers/mouse/mouse.h"
#include "drivers/screen/screen.h"

isr_t interrupt_handlers[256];

/* Can't do this with a loop because we need the address
 * of the function names */
void InstallISR() 
{
    SetIDTGate(0, IDT_TA_InterruptGate, (uint64_t)isr0);
    SetIDTGate(1, IDT_TA_InterruptGate, (uint64_t)isr1);
    SetIDTGate(2, IDT_TA_InterruptGate, (uint64_t)isr2);
    SetIDTGate(3, IDT_TA_InterruptGate, (uint64_t)isr3);
    SetIDTGate(4, IDT_TA_InterruptGate, (uint64_t)isr4);
    SetIDTGate(5, IDT_TA_InterruptGate, (uint64_t)isr5);
    SetIDTGate(6, IDT_TA_InterruptGate, (uint64_t)isr6);
    SetIDTGate(7, IDT_TA_InterruptGate, (uint64_t)isr7);
    SetIDTGate(8, IDT_TA_InterruptGate, (uint64_t)isr8);
    SetIDTGate(9, IDT_TA_InterruptGate, (uint64_t)isr9);
    SetIDTGate(10, IDT_TA_InterruptGate, (uint64_t)isr10);
    SetIDTGate(11, IDT_TA_InterruptGate, (uint64_t)isr11);
    SetIDTGate(12, IDT_TA_InterruptGate, (uint64_t)isr12);
    SetIDTGate(13, IDT_TA_InterruptGate, (uint64_t)isr13);
    SetIDTGate(14, IDT_TA_InterruptGate, (uint64_t)isr14);
    SetIDTGate(15, IDT_TA_InterruptGate, (uint64_t)isr15);
    SetIDTGate(16, IDT_TA_InterruptGate, (uint64_t)isr16);
    SetIDTGate(17, IDT_TA_InterruptGate, (uint64_t)isr17);
    SetIDTGate(18, IDT_TA_InterruptGate, (uint64_t)isr18);
    SetIDTGate(19, IDT_TA_InterruptGate, (uint64_t)isr19);
    SetIDTGate(20, IDT_TA_InterruptGate, (uint64_t)isr20);
    SetIDTGate(21, IDT_TA_InterruptGate, (uint64_t)isr21);
    SetIDTGate(22, IDT_TA_InterruptGate, (uint64_t)isr22);
    SetIDTGate(23, IDT_TA_InterruptGate, (uint64_t)isr23);
    SetIDTGate(24, IDT_TA_InterruptGate, (uint64_t)isr24);
    SetIDTGate(25, IDT_TA_InterruptGate, (uint64_t)isr25);
    SetIDTGate(26, IDT_TA_InterruptGate, (uint64_t)isr26);
    SetIDTGate(27, IDT_TA_InterruptGate, (uint64_t)isr27);
    SetIDTGate(28, IDT_TA_InterruptGate, (uint64_t)isr28);
    SetIDTGate(29, IDT_TA_InterruptGate, (uint64_t)isr29);
    SetIDTGate(30, IDT_TA_InterruptGate, (uint64_t)isr30);
    SetIDTGate(31, IDT_TA_InterruptGate, (uint64_t)isr31);

    // Install the IRQs
    SetIDTGate(IRQ0, IDT_TA_InterruptGate, (uint64_t)irq0);
    SetIDTGate(IRQ1, IDT_TA_InterruptGate, (uint64_t)irq1);
    SetIDTGate(IRQ2, IDT_TA_InterruptGate, (uint64_t)irq2);
    SetIDTGate(IRQ3, IDT_TA_InterruptGate, (uint64_t)irq3);
    SetIDTGate(IRQ4, IDT_TA_InterruptGate, (uint64_t)irq4);
    SetIDTGate(IRQ5, IDT_TA_InterruptGate, (uint64_t)irq5);
    SetIDTGate(IRQ6, IDT_TA_InterruptGate, (uint64_t)irq6);
    SetIDTGate(IRQ7, IDT_TA_InterruptGate, (uint64_t)irq7);
    SetIDTGate(IRQ8, IDT_TA_InterruptGate, (uint64_t)irq8);
    SetIDTGate(IRQ9, IDT_TA_InterruptGate, (uint64_t)irq9);
    SetIDTGate(IRQ10, IDT_TA_InterruptGate, (uint64_t)irq10);
    SetIDTGate(IRQ11, IDT_TA_InterruptGate, (uint64_t)irq11);
    SetIDTGate(IRQ12, IDT_TA_InterruptGate, (uint64_t)irq12);
    SetIDTGate(IRQ13, IDT_TA_InterruptGate, (uint64_t)irq13);
    SetIDTGate(IRQ14, IDT_TA_InterruptGate, (uint64_t)irq14);
    SetIDTGate(IRQ15, IDT_TA_InterruptGate, (uint64_t)irq15);

    //Load the IDT in assembly
    SetIDT();

    // Remap the PIC
    uint8_t a1, a2;
    a1 = __inb(PIC1_DATA);
    io_wait();
    a2 = __inb(PIC2_DATA);
    io_wait();

    __outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    __outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    __outb(PIC1_DATA, 0x20);
    io_wait();
    __outb(PIC2_DATA, 0x28);
    io_wait();

    __outb(PIC1_DATA, 4);
    io_wait();
    __outb(PIC2_DATA, 2);
    io_wait();

    __outb(PIC1_DATA, ICW4_8086);
    io_wait();
    __outb(PIC2_DATA, ICW4_8086);
    io_wait();

    __outb(PIC1_DATA, a1);
    io_wait();
    __outb(PIC2_DATA, a2); 
    io_wait();
}

/* To print the message which defines every exception */
char *exception_messages[] = 
{
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",

    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",

    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",

    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

void ISRHandler(registers_t* r) 
{
    if (interrupt_handlers[r->int_no] != 0)
    {
        isr_t handler = interrupt_handlers[r->int_no];
        handler(r);
    }
    else
    {
        kprintf("Recieved interrupt without a handler: %d\n", r->int_no);
    }
}

void RegisterInterruptHandler(uint8_t index, isr_t handler) 
{
    interrupt_handlers[index] = handler;
}

void IRQHandler(registers_t* regs) 
{
    if (regs->int_no >= 40) 
        __outb(0xA0, 0x20);
    
    __outb(0x20, 0x20);

    if (interrupt_handlers[regs->int_no] != 0) 
    {
        isr_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    }
}

void InstallIRQ()
{
    InitTimer(1000);
    InitialiseMouse(GetCurrentDisplay()->framebuffer.width, 
        GetCurrentDisplay()->framebuffer.height);
    InitKeyboard();

    __outb(PIC1_DATA, 0b11111000);
    __outb(PIC2_DATA, 0b11101111);

    asm("sti");
}