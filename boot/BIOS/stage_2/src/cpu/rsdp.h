#ifndef RSDP_H
#define RSDP_H

#include "libc/stdint.h"

typedef struct rsdp1
{
    char signiture[8];
    uint8_t checksum;
    char OEMID[6];
    uint8_t revision;
    uint32_t RSDT_address;
} __attribute__((packed)) RSDP1;

typedef struct rsdp2
{
    char signiture[8];
    uint8_t checksum;
    char OEMID[6];
    uint8_t revision;
    uint32_t RSDT_address;

    uint32_t length;
    uint64_t XSDT_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) RSDP2;

void* FindRSDP(uint8_t* version);

#endif