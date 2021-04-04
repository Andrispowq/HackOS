#ifndef RSDP_H
#define RSDP_H

#include "lib/stdint.h"
#include "system_table.h"

class RSDP
{
public:
    ACPI::SDTHeader* GetRootSystemTable();
    ACPI::SDTHeader* GetSystemTable(const char* signature);
    bool is_valid();

public:
    char Signature[8];
    uint8_t Checksum;
    char OEMID[6];
    uint8_t Revision;
    uint32_t RSDTAddress;

    uint32_t Length;
    uint64_t XSDTAddress;
    uint8_t ExtendedChecksum;
    uint8_t Reserved[3];
} __attribute__((packed));

#endif