#ifndef TSS_H
#define TSS_H

#include "lib/stdint.h"

struct TSS_Struct
{
    uint32_t rsv0;
    uint64_t rsp[3];
    uint64_t rsv1;
    uint64_t ist[7];
    uint64_t rsv2;
    uint16_t rsv3;
    uint16_t iobp_off;
} __attribute__((packed));

#endif