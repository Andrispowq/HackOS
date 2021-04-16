#include "rsdp.h"

namespace ACPI
{
    SDTHeader* RSDP::GetRootSystemTable()
    {
        SDTHeader* sdtHeader;
        if(Revision > 0)
        {
            sdtHeader = (SDTHeader*)XSDTAddress;
        }
        else
        {
            sdtHeader = (SDTHeader*)(uint64_t)RSDTAddress;
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
        SDTHeader* hdr = GetRootSystemTable();

        uint64_t divisor = 4;
        if(Revision > 0)
        {
            divisor = 8;
        }

        return (hdr->Length - sizeof(SDTHeader)) / divisor;
    }

    SDTHeader* RSDP::Get(uint64_t index)
    {
        uint64_t entries = GetTableCount();
        if(entries <= index)
        {
            return nullptr;
        }

        SDTHeader* hdr = GetRootSystemTable();
        uint64_t ptr = (uint64_t)hdr;
        ptr += sizeof(SDTHeader);

        if(Revision > 0)
        {
            uint64_t* ent = (uint64_t*)ptr;
            return (SDTHeader*)ent[index];
        }
        else
        {
            uint32_t* ent = (uint32_t*)ptr;
            return (SDTHeader*)(uint64_t)ent[index];
        }
    }

    SDTHeader* RSDP::GetSystemTable(const char* signature)
    {
        ACPI::SDTHeader* hdr = GetRootSystemTable();
        uint64_t entries = GetTableCount();

        for (uint64_t t = 0; t < entries; t++)
        {
            uint64_t ptr = (uint64_t)hdr;
            ptr += sizeof(SDTHeader);
            SDTHeader* newSDTHeader = nullptr;
            if(Revision > 0)
            {
                uint64_t* ent = (uint64_t*)ptr;
                newSDTHeader = (SDTHeader*)ent[t];
            }
            else
            {
                uint32_t* ent = (uint32_t*)ptr;
                newSDTHeader = (SDTHeader*)(uint64_t)ent[t];
            }

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

};