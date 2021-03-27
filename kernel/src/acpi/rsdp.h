#ifndef RSDP_H
#define RSDP_H

#include "lib/stdint.h"

typedef struct rsdp1
{
    char Signature[8];
    uint8_t Checksum;
    char OEMID[6];
    uint8_t Revision;
    uint32_t RSDTAddress;
} __attribute__((packed)) RSDP1;

typedef struct rsdp2
{
    RSDP1 rsdp1;

    uint32_t Length;
    uint64_t XSDTAddress;
    uint8_t ExtendedChecksum;
    uint8_t Reserved[3];
} __attribute__((packed)) RSDP2;

#endif