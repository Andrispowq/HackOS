#include "fadt.h"

#include "arch/x86_64/ports.h"
#include "arch/x86_64/timer/pit.h"

#include "lib/stdio.h"

namespace ACPI
{
    uint32_t* SMI_CMD;

    uint8_t ACPI_ENABLE;
    uint8_t ACPI_DISABLE;

    uint32_t* PM1a_CNT;
    uint32_t* PM1b_CNT;

    uint8_t PM1_CNT_LEN;

    uint16_t SLP_EN;
    uint16_t SCI_EN;

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

    DSDT* FADT::GetDSDT()
    {
        if(_fadt.Dsdt != 0)
        {
            return (DSDT*)(uint64_t)_fadt.Dsdt;
        }
        else
        {
            return (DSDT*)_fadt.X_Dsdt;
        }
    }

    void FADT::ACPIEnable()
    {
        DSDT* dsdt = GetDSDT();
        dsdt->Get_S5_Object();

        SMI_CMD = (uint32_t*)(uint64_t)_fadt.SMI_CommandPort;

        ACPI_ENABLE = _fadt.AcpiEnable;
        ACPI_DISABLE = _fadt.AcpiDisable;

        PM1a_CNT = (uint32_t*)(uint64_t)_fadt.PM1aControlBlock;
        PM1b_CNT = (uint32_t*)(uint64_t)_fadt.PM1bControlBlock;
                     
        PM1_CNT_LEN = _fadt.PM1ControlLength;

        SLP_EN = 1 << 13;
        SCI_EN = 1;

        //Enable ACPI
        if ((__inw((uint16_t)(uint64_t)PM1a_CNT) & SCI_EN) == 0)
        {
            // check if acpi can be enabled
            if (SMI_CMD != 0 && ACPI_ENABLE != 0)
            {
                __outb((uint8_t)(uint64_t)SMI_CMD, ACPI_ENABLE); // send acpi enable command
                // give 3 seconds time to enable acpi
                int i;
                for (i = 0; i < 300; i++ )
                {
                    if ((__inw((uint16_t)(uint64_t)PM1a_CNT) & SCI_EN) == 1)
                    {
                        break;
                    }
                    
                    SleepFor(10);
                }

                if (PM1b_CNT != 0)
                {
                    for (; i < 300; i++ )
                    {
                        if ((__inw((uint16_t)(uint64_t)PM1b_CNT) & SCI_EN) == 1)
                        {
                            break;
                        }

                        SleepFor(10);
                    }
                }
                
                if (i < 300) 
                {
                    kprintf("Enabled ACPI\n");
                    return;
                } 
                else 
                {
                    kprintf("Couldn't enable ACPI\n");
                }
            } 
            else 
            {
                kprintf("No known way to enable ACPI\n");
            }
        }
    }

    void FADT::Shutdown()
    {
        if(SCI_EN != 1)
        {
            return;
        }

        __outw((uint16_t)(uint64_t)PM1a_CNT, SLP_TYPa | SLP_EN);
        if(PM1b_CNT != 0)
        {
            __outw((uint16_t)(uint64_t)PM1b_CNT, SLP_TYPb | SLP_EN);
        }

        kprintf("ACPI shutdown failed!\n");
    }
};