#ifndef PAGE_TABLES_H
#define PAGE_TABLES_H

struct PageTableEntry
{
    uint64_t present                   : 1;
    uint64_t writeable                 : 1;
    uint64_t user_access               : 1;
    uint64_t write_through             : 1;
    uint64_t cache_disabled            : 1;
    uint64_t accessed                  : 1;
    uint64_t dirty                     : 1;
    uint64_t size                      : 1;
    uint64_t global                    : 1;
    uint64_t ignored_2                 : 3;
    uint64_t address                   : 28;
    uint64_t reserved_1                : 12;
    uint64_t ignored_1                 : 11;
    uint64_t execution_disabled        : 1;
};

struct PageTable
{
    PageTableEntry entries[512];
};

#endif