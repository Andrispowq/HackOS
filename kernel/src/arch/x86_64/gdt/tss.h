#ifndef TSS_H
#define TSS_H

#include "lib/stdint.h"

struct TSS_Struct
{
    uint32_t rsv0;

    uint32_t rsp0_lo;
    uint32_t rsp0_hi;
    uint32_t rsp1_lo;
    uint32_t rsp1_hi;
    uint32_t rsp2_lo;
    uint32_t rsp2_hi;

    uint32_t rsv1;
    uint32_t rsv2;

    uint32_t ist1_lo;
    uint32_t ist1_hi;
    uint32_t ist2_lo;
    uint32_t ist2_hi;
    uint32_t ist3_lo;
    uint32_t ist3_hi;
    uint32_t ist4_lo;
    uint32_t ist4_hi;
    uint32_t ist5_lo;
    uint32_t ist5_hi;
    uint32_t ist6_lo;
    uint32_t ist6_hi;
    uint32_t ist7_lo;
    uint32_t ist7_hi;

    uint32_t rsv3;
    uint32_t rsv4;

    uint32_t iobp_off;
};

#endif