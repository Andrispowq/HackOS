#ifndef PORTS_H
#define PORTS_H

#include "../libc/stdint.h"

uint8_t inb(uint16_t port);
uint16_t inw(uint16_t port);
uint32_t inl(uint32_t port);

void outb(uint16_t port, uint8_t data);
void outw(uint16_t port, uint16_t data);
void outl(uint32_t port, uint32_t data);

#endif