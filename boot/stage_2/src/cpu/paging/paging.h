#ifndef PAGING_H
#define PAGING_H

#include "libc/stdint.h"

#include "cpu/idt.h"
#include "cpu/isr.h"

typedef struct page_table_entry
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
    uint64_t reserved_1                : 12; // must be 0
    uint64_t ignored_1                 : 11;
    uint64_t execution_disabled        : 1;
} page_table_entry_t;

typedef struct page_directory_entry
{
    uint64_t present                   : 1;
    uint64_t writeable                 : 1;
    uint64_t user_access               : 1;
    uint64_t write_through             : 1;
    uint64_t cache_disabled            : 1;
    uint64_t accessed                  : 1;
    uint64_t ignored_3                 : 1;
    uint64_t size                      : 1; // 0 means page table mapped
    uint64_t ignored_2                 : 4;
    uint64_t address                   : 28;
    uint64_t reserved_1                : 12; // must be 0
    uint64_t ignored_1                 : 11;
    uint64_t execution_disabled        : 1;
} page_directory_entry_t;

typedef struct page_directory_pointer_entry
{
    uint64_t present                   : 1;
    uint64_t writeable                 : 1;
    uint64_t user_access               : 1;
    uint64_t write_through             : 1;
    uint64_t cache_disabled            : 1;
    uint64_t accessed                  : 1;
    uint64_t ignored_3                 : 1;
    uint64_t size                      : 1; // 0 means page directory mapped
    uint64_t ignored_2                 : 4;
    uint64_t address                   : 28;
    uint64_t reserved_1                : 12; // must be 0
    uint64_t ignored_1                 : 11;
} page_directory_pointer_entry_t;

typedef struct page_map_level_4_entry
{
    uint64_t present                   : 1;
    uint64_t writeable                 : 1;
    uint64_t user_access               : 1;
    uint64_t write_through             : 1;
    uint64_t cache_disabled            : 1;
    uint64_t accessed                  : 1;
    uint64_t ignored_3                 : 1;
    uint64_t size                      : 1; // must be 0
    uint64_t ignored_2                 : 4;
    uint64_t address                   : 28;
    uint64_t reserved_1                : 12; // must be 0
    uint64_t ignored_1                 : 11;
    uint64_t execution_disabled        : 1;
} page_map_level_4_entry_t;

typedef struct page_table
{
    page_table_entry_t entries[512];
} page_table_t;

typedef struct page_directory
{
    page_directory_entry_t entries[512];
} page_directory_t;

typedef struct page_directory_pointer
{
    page_directory_pointer_entry_t entries[512];
} page_directory_pointer_t;

typedef struct page_map_level_4
{
    page_map_level_4_entry_t entries[512];
} page_map_level_4_t;

typedef struct page_map_index
{
    uint64_t index_PDP;
    uint64_t index_PD;
    uint64_t index_PT;
    uint64_t index_P;
} page_map_index_t;

page_map_index_t IndexOf(vaddr_t address);

void InitPaging();

void MapMemory(vaddr_t virtualAddress, paddr_t physicalAddress);
void PageFaultHandler(registers_t* regs);

uint64_t GetUsedMemory();
uint64_t GetFreeMemory();
uint64_t GetReservedMemory();

#endif