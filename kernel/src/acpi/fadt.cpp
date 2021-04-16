#include "fadt.h"

namespace ACPI
{
    bool FADT::is_valid()
    {
        uint8_t* ptr = (uint8_t*)this;    
        uint64_t size = sizeof(FADT);

        uint8_t sum = 0;
        for(uint64_t i = 0; i < size; i++)
        {
            sum += ptr[i];
        }

        return sum == 0;
    }
};