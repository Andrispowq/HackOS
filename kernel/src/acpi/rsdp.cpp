#include "rsdp.h"

ACPI::SDTHeader* RSDP::GetRootSystemTable()
{
    ACPI::SDTHeader* sdtHeader;
    if(Revision > 0)
    {
        sdtHeader = (ACPI::SDTHeader*)XSDTAddress;
    }
    else
    {
        sdtHeader = (ACPI::SDTHeader*)(uint64_t)RSDTAddress;
    }

    return sdtHeader;
}

ACPI::SDTHeader* RSDP::GetSystemTable(const char* signature)
{
    ACPI::SDTHeader* sdtHeader = GetRootSystemTable();

    uint64_t entries = (sdtHeader->Length - sizeof(ACPI::SDTHeader)) / 8;

    for (uint64_t t = 0; t < entries; t++)
    {
        ACPI::SDTHeader* newSDTHeader = (ACPI::SDTHeader*)*(uint64_t*)((uint64_t)sdtHeader + sizeof(ACPI::SDTHeader) + (t * 8));
        for (int i = 0; i < 4; i++)
        {
            if (newSDTHeader->Signature[i] != signature[i])
            {
                break;
            }

            if (i == 3) 
            {
                return newSDTHeader;
            }
        }
    }
    
    return 0;
}

bool RSDP::is_valid()
{
    uint8_t* ptr = (uint8_t*)this;    
    uint64_t size;

    if(Revision > 0)
    {
        size = sizeof(RSDP);
    }
    else
    {
        size = 20;
    }

    uint8_t sum = 0;
    for(uint64_t i = 0; i < size; i++)
    {
        sum += ptr[i];
    }

    return sum == 0;
}