#include "rsdp.h"
#include "libc/stdio.h"

static int DoChecksum(void* rsdp)
{
    uint8_t* ptr = (uint8_t*)rsdp;    
    uint64_t size;

    if(((RSDP2*)rsdp)->revision > 0)
    {
        size = sizeof(RSDP2);
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
                }
            }
        }
    }
    
    *version = v2;

    return ptr;
}