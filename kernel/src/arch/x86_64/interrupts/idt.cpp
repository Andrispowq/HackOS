#include "idt.h"

#include "arch/x86_64/ports.h"
#include "lib/stdio.h"

#include "arch/x86_64/timer/pit.h"
#include "drivers/keyboard/keyboard.h"
#include "drivers/mouse/mouse.h"
#include "drivers/screen/screen.h"

static void DefaultExceptionHandler(Registers* registers)
{
    kprintf("Recieved interrupt #%x with error code %x on the default handler!\n", 
        registers->int_no, registers->err_code);
    kprintf("RIP: %x, RSP: %x\n", registers->rip, registers->rsp);
    asm("cli; hlt");
}

void IDTR::SetGate(uint8_t entry, uint8_t flags, uint64_t handler) 
{
    offset[entry].offset0 = (uint16_t)(handler & 0x000000000000FFFF);
    offset[entry].offset1 = (uint16_t)((handler & 0x00000000FFFF0000) >> 16);
    offset[entry].offset2 = (uint32_t)((handler & 0xFFFFFFFF00000000) >> 32);

    offset[entry].selector = KERNEL_CS;
    offset[entry].flags = flags;
}

void IDTR::Flush() 
{
    __set_idt(this);
}

static IDTGate gates[IDT_ENTRIES];
IDTR idtr;
void InitialiseIDT()
{
    idtr.limit = sizeof(IDTGate) * IDT_ENTRIES - 1;
    idtr.offset = gates;

    //Install the IDTs
    idtr.SetGate(0, IDT_TA_InterruptGate, (uint64_t)isr0);
    idtr.SetGate(1, IDT_TA_InterruptGate, (uint64_t)isr1);
    idtr.SetGate(2, IDT_TA_InterruptGate, (uint64_t)isr2);
    idtr.SetGate(3, IDT_TA_InterruptGate, (uint64_t)isr3);
    idtr.SetGate(4, IDT_TA_InterruptGate, (uint64_t)isr4);
    idtr.SetGate(5, IDT_TA_InterruptGate, (uint64_t)isr5);
    idtr.SetGate(6, IDT_TA_InterruptGate, (uint64_t)isr6);
    idtr.SetGate(7, IDT_TA_InterruptGate, (uint64_t)isr7);
    idtr.SetGate(8, IDT_TA_InterruptGate, (uint64_t)isr8);
    idtr.SetGate(9, IDT_TA_InterruptGate, (uint64_t)isr9);
    idtr.SetGate(10, IDT_TA_InterruptGate, (uint64_t)isr10);
    idtr.SetGate(11, IDT_TA_InterruptGate, (uint64_t)isr11);
    idtr.SetGate(12, IDT_TA_InterruptGate, (uint64_t)isr12);
    idtr.SetGate(13, IDT_TA_InterruptGate, (uint64_t)isr13);
    idtr.SetGate(14, IDT_TA_InterruptGate, (uint64_t)isr14);
    idtr.SetGate(15, IDT_TA_InterruptGate, (uint64_t)isr15);
    idtr.SetGate(16, IDT_TA_InterruptGate, (uint64_t)isr16);
    idtr.SetGate(17, IDT_TA_InterruptGate, (uint64_t)isr17);
    idtr.SetGate(18, IDT_TA_InterruptGate, (uint64_t)isr18);
    idtr.SetGate(19, IDT_TA_InterruptGate, (uint64_t)isr19);
    idtr.SetGate(20, IDT_TA_InterruptGate, (uint64_t)isr20);
    idtr.SetGate(21, IDT_TA_InterruptGate, (uint64_t)isr21);
    idtr.SetGate(22, IDT_TA_InterruptGate, (uint64_t)isr22);
    idtr.SetGate(23, IDT_TA_InterruptGate, (uint64_t)isr23);
    idtr.SetGate(24, IDT_TA_InterruptGate, (uint64_t)isr24);
    idtr.SetGate(25, IDT_TA_InterruptGate, (uint64_t)isr25);
    idtr.SetGate(26, IDT_TA_InterruptGate, (uint64_t)isr26);
    idtr.SetGate(27, IDT_TA_InterruptGate, (uint64_t)isr27);
    idtr.SetGate(28, IDT_TA_InterruptGate, (uint64_t)isr28);
    idtr.SetGate(29, IDT_TA_InterruptGate, (uint64_t)isr29);
    idtr.SetGate(30, IDT_TA_InterruptGate, (uint64_t)isr30);
    idtr.SetGate(31, IDT_TA_InterruptGate, (uint64_t)isr31);

    // Install the IRQs
    idtr.SetGate(IRQ0, IDT_TA_InterruptGate, (uint64_t)irq0);
    idtr.SetGate(IRQ1, IDT_TA_InterruptGate, (uint64_t)irq1);
    idtr.SetGate(IRQ2, IDT_TA_InterruptGate, (uint64_t)irq2);
    idtr.SetGate(IRQ3, IDT_TA_InterruptGate, (uint64_t)irq3);
    idtr.SetGate(IRQ4, IDT_TA_InterruptGate, (uint64_t)irq4);
    idtr.SetGate(IRQ5, IDT_TA_InterruptGate, (uint64_t)irq5);
    idtr.SetGate(IRQ6, IDT_TA_InterruptGate, (uint64_t)irq6);
    idtr.SetGate(IRQ7, IDT_TA_InterruptGate, (uint64_t)irq7);
    idtr.SetGate(IRQ8, IDT_TA_InterruptGate, (uint64_t)irq8);
    idtr.SetGate(IRQ9, IDT_TA_InterruptGate, (uint64_t)irq9);
    idtr.SetGate(IRQ10, IDT_TA_InterruptGate, (uint64_t)irq10);
    idtr.SetGate(IRQ11, IDT_TA_InterruptGate, (uint64_t)irq11);
    idtr.SetGate(IRQ12, IDT_TA_InterruptGate, (uint64_t)irq12);
    idtr.SetGate(IRQ13, IDT_TA_InterruptGate, (uint64_t)irq13);
    idtr.SetGate(IRQ14, IDT_TA_InterruptGate, (uint64_t)irq14);
    idtr.SetGate(IRQ15, IDT_TA_InterruptGate, (uint64_t)irq15);

    for(uint8_t i = 0; i < 32; i++)
    {
        RegisterInterruptHandler(i, DefaultExceptionHandler);
    }

    //Load the IDT in assembly
    idtr.Flush();

    //Initialise the PIC
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
}

void InitialiseIRQ()
{
    InitTimer(1000);
    InitKeyboard();
    InitialiseMouse(Display::SharedDisplay()->framebuffer.width, 
        Display::SharedDisplay()->framebuffer.height);

    __outb(PIC1_DATA, 0b11111000);
    __outb(PIC2_DATA, 0b11111111);

    asm volatile("sti");
}

//ISR handlers
static isr_t interrupt_handlers[256];

extern "C"
{
    void ISRHandler(Registers* r) 
    {
        if (interrupt_handlers[r->int_no] != 0)
        {
            isr_t handler = interrupt_handlers[r->int_no];
            handler(r);
        }
        else
        {
            kprintf("Recieved interrupt without a handler: %d\n", r->int_no);
            while(1) asm("hlt");
        }
    }
    
    void IRQHandler(Registers* regs) 
    {
        if (interrupt_handlers[regs->int_no] != 0) 
        {
            isr_t handler = interrupt_handlers[regs->int_no];
            handler(regs);
        }

        if (regs->int_no >= 40)
        {
            __outb(PIC2_COMMAND, PIC_EOI);
        }
        
        __outb(PIC1_COMMAND, PIC_EOI);
    }
}

void RegisterInterruptHandler(uint8_t index, isr_t handler) 
{
    interrupt_handlers[index] = handler;
}

/* To print the message which defines every exception */
const char* exception_messages[] = 
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