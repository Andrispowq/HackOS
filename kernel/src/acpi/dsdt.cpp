#include "dsdt.h"

#include "lib/memory.h"

namespace ACPI
{
    uint16_t SLP_TYPa = 0;
    uint16_t SLP_TYPb = 0;

    void DSDT::Get_S5_Object()
    {
        //If we already found it, don't parse
        if(SLP_TYPa != 0 && SLP_TYPb != 0)
        {
            return;
        }

        uint32_t DSDTLength = header.Length - sizeof(SDTHeader);
        char* S5Addr = (char*)&header + sizeof(SDTHeader);
        while(0 < DSDTLength--)
        {
            if(memcmp(S5Addr, (char*)"_S5_", 4) == 0)
            {
                break;
            }

            S5Addr++;
        }

        if(DSDTLength > 0)
        {
            // check for valid AML structure
            if ((*(S5Addr - 1) == 0x08 || (*(S5Addr - 2) == 0x08 && *(S5Addr - 1) == '\\')) && *(S5Addr + 4) == 0x12)
            {
                S5Addr += 5;
                S5Addr += ((*S5Addr & 0xC0) >> 6) + 2;   // calculate PkgLength size

                if (*S5Addr == 0x0A)
                {
                    S5Addr++;   // skip byteprefix
                }

                SLP_TYPa = *(S5Addr)<<10;
                S5Addr++;

                if (*S5Addr == 0x0A)
                {
                    S5Addr++;   // skip byteprefix
                }

                SLP_TYPb = *(S5Addr)<<10;
             }
        }
    }
};