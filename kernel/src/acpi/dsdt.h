#ifndef DSDT_H
#define DSDT_H

#include "system_table.h"

namespace ACPI
{
    extern uint16_t SLP_TYPa;
    extern uint16_t SLP_TYPb;

    class DSDT
    {
    public:
        void Get_S5_Object();

    private:
        SDTHeader header;
    };
};

#endif