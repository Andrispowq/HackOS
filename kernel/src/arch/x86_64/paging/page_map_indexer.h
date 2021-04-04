#ifndef PAGE_MAP_INDEXER_H
#define PAGE_MAP_INDEXER_H

#include "lib/stdint.h"

class PageMapIndexer
{
public:
    PageMapIndexer(uint64_t virtualAddress)
    {
        virtualAddress >>= 12;
        index_P = virtualAddress & 0x1ff;
        virtualAddress >>= 9;
        index_PT = virtualAddress & 0x1ff;
        virtualAddress >>= 9;
        index_PD = virtualAddress & 0x1ff;
        virtualAddress >>= 9;
        index_PDP = virtualAddress & 0x1ff;
    }

public:
    uint64_t index_PDP;
    uint64_t index_PD;
    uint64_t index_PT;
    uint64_t index_P;
};

#endif