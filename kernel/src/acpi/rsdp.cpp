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

uint64_t RSDP::GetTableCount()
{
    ACPI::SDTHeader* hdr = GetRootSystemTable();

    return (hdr->Length - sizeof(ACPI::SDTHeader)) / 8;
}

ACPI::SDTHeader* RSDP::Get(uint64_t index)
{
    uint64_t entries = GetTableCount();
    if(entries <= index)
    {
        return nullptr;
    }

    ACPI::SDTHeader* hdr = GetRootSystemTable();
    uint64_t ptr = (uint64_t)hdr;
    ptr += sizeof(ACPI::SDTHeader);
    ptr += index * 8;
    uint64_t* _ptr = (uint64_t*)ptr;
    return (ACPI::SDTHeader*)*_ptr;
}

ACPI::SDTHeader* RSDP::GetSystemTable(const char* signature)
{
    ACPI::SDTHeader* hdr = GetRootSystemTable();
    uint64_t entries = GetTableCount();

    for (uint64_t t = 0; t < entries; t++)
    {
        uint64_t ptr = (uint64_t)hdr;
        ptr += sizeof(ACPI::SDTHeader);
        ptr += t * 8;
        uint64_t* _ptr = (uint64_t*)ptr;
        ACPI::SDTHeader* newSDTHeader = (ACPI::SDTHeader*)*_ptr;
        
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