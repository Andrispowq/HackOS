#ifndef RSDP_H
#define RSDP_H

#include "lib/stdint.h"
#include "system_table.h"

class RSDP
{
public:
    ACPI::SDTHeader* GetRootSystemTable();
    bool is_valid();

    uint64_t GetTableCount();
    ACPI::SDTHeader* Get(uint64_t index);
    ACPI::SDTHeader* GetSystemTable(const char* signature);
    
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