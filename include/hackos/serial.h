#pragma once

// Freestanding x86 COM1 diagnostics, shared by the C loaders and C++ kernel.
// No interrupts, allocation, timers, or graphics; every hardware wait is bounded.
static inline void HackosSerialOut(unsigned short port, unsigned char value)
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline unsigned char HackosSerialIn(unsigned short port)
{
    unsigned char value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline int HackosSerialInitialise(void)
{
    HackosSerialOut(0x3f9, 0);
    HackosSerialOut(0x3fb, 0x80);
    HackosSerialOut(0x3f8, 1); // 115200 baud, 8 data bits, no parity, one stop bit
    HackosSerialOut(0x3f9, 0);
    HackosSerialOut(0x3fb, 3);
    HackosSerialOut(0x3fa, 0xc7);
    HackosSerialOut(0x3fc, 0x1e);
    HackosSerialOut(0x3f8, 0xae);
    int present = 0;
    for (unsigned i = 0; i < 10000; ++i)
    {
        unsigned char status = HackosSerialIn(0x3fd);
        if (status == 0xff)
        {
            break;
        }

        if (status & 1)
        {
            present = HackosSerialIn(0x3f8) == 0xae;
            break;
        }
    }

    HackosSerialOut(0x3fc, 3);
    return present;
}

static inline int HackosSerialPut(char ch)
{
    for (unsigned i = 0; i < 10000; ++i)
    {
        unsigned char status = HackosSerialIn(0x3fd);
        if (status == 0xff)
        {
            return 0;
        }

        if (status & 0x20)
        {
            HackosSerialOut(0x3f8, (unsigned char)ch);
            return 1;
        }
    }

    return 0;
}
