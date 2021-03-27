#include "rsdp.h"
#include "libc/stdio.h"

static int DoChecksum(void* rsdp)
{
    int i;
    uint8_t check = 0;
    uint8_t* RSDP_ptr = (uint8_t*)rsdp;
    int v2 = 0;

    if (((RSDP1*)RSDP_ptr)->revision == 0)     // Checksum ACPI v1
    {
        for (i = 0; i < (int)sizeof(RSDP1); i++)
        {
            check += RSDP_ptr[i];
        }

        v2 = 0;
    }
    else if (((RSDP2*)RSDP_ptr)->revision > 0)  // Checksum ACPI v2+
    {
        for (i = 0; i < (int)sizeof(RSDP2); i++)
        {
            check += RSDP_ptr[i];
        }

        v2 = 1;
    }

    if (check != 0)
    {
        return -1;
    }

    return v2;
}

void* FindRSDP(uint8_t* version)
{
    //The EBDA is usually found at the location pointed to by the memory at 0x40E
    //But this (on this device) is at 0x9FC0, which is in the middle of the second stage code
    //So we copy it in the first stage bootloader to 0xF000 which should be free to use
    uint16_t* EBDA_addr = (uint16_t*)0x40E;
    uint16_t* EBDA_size_addr = (uint16_t*)0x413;
    uint8_t* EBDA_ptr = (uint8_t*)((uint64_t)(*EBDA_addr) << 4);
    uint16_t EBDA_size = *EBDA_size_addr * 1024;

    void* ptr = 0;
    int v2 = 0;

    for(uint64_t i = EBDA_ptr; i < (EBDA_ptr + EBDA_size); i += 0x10)
    {
        void* curr = (void*)i;
        if(strncmp((char*)curr, (char*)"RSD PTR ", 8) == 0)
        {
            void* possible_RSDP = (void*)curr;
            int checksum = DoChecksum(possible_RSDP);

            if(checksum == 1)
            {
                ptr = possible_RSDP;
                v2 = 1;
                break;
            }
            else if(checksum == 0)
            {
                ptr = possible_RSDP;
                v2 = 0;
            }
        }
    }

    if(ptr == 0)
    {
        uint8_t* EXT_mem_start = (uint8_t*)0x000E0000;
        uint8_t* EXT_mem_end = (uint8_t*)0x000FFFFF;

        for(uint64_t i = EXT_mem_start; i < EXT_mem_end; i += 0x10)
        {
            void* curr = (void*)i;

            if(strncmp((char*)curr, (char*)"RSD PTR ", 8) == 0)
            {
                void* possible_RSDP = (void*)curr;
                int checksum = DoChecksum(possible_RSDP);

                if(checksum == 1)
                {
                    ptr = possible_RSDP;
                    v2 = 1;
                    break;
                }
                else if(checksum == 0)
                {
                    ptr = possible_RSDP;
                    v2 = 0;
                }
            }
        }
    }
    
    *version = v2;

    return ptr;
}