#ifndef PORTS_H
#define PORTS_H

#include "lib/stdint.h"

uint8_t __inb(uint16_t port);
uint16_t __inw(uint16_t port);
uint32_t __inl(uint32_t port);

void __outb(uint16_t port, uint8_t data);
void __outw(uint16_t port, uint16_t data);
void __outl(uint32_t port, uint32_t data);

void io_wait();

class Port8
{
public:
    Port8(uint16_t port)
    {
        this->port = port;
    }

    uint8_t Read() const { return __inb(port); }
    void Write(uint8_t value) { __outb(port, value); }

private:
    uint16_t port;
};

class Port16
{
public:
    Port16(uint16_t port)
    {
        this->port = port;
    }

    uint16_t Read() const { return __inw(port); }
    void Write(uint16_t value) { __outw(port, value); }

private:
    uint16_t port;
};

class Port32
{
public:
    Port32(uint32_t port)
    {
        this->port = port;
    }

    uint32_t Read() const { return __inl(port); }
    void Write(uint32_t value) { __outl(port, value); }

private:
    uint32_t port;
};

#endif