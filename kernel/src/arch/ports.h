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

#endif