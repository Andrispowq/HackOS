#pragma once
#include "arch/x86_64/paging/page_table_manager.h"

// One private 2 MiB window in an otherwise unused PML4 slot. Kernel mappings
// remain supervisor-only; each process owns the four tables and all leaf frames.
constexpr uint64_t USER_BASE = 0x0000400000000000ULL;
constexpr uint64_t USER_END = USER_BASE + 0x200000;
constexpr uint64_t USER_STACK_BOTTOM = USER_END - 0x4000;
constexpr uint64_t USER_IMAGE_END = USER_STACK_BOTTOM - 0x1000; // stack guard
constexpr uint64_t USER_MAX_PAGES = 64;
extern uint64_t user_pages_live;
extern bool user_nx_enabled;

class UserAddressSpace
{
public:
    UserAddressSpace() : directory(nullptr), leaf(nullptr), ownedCount(0) {}
    ~UserAddressSpace();
    bool Initialise();
    void* AddPage(uint64_t address, bool writable, bool executable);
    bool CopyFromUser(void* destination, uint64_t source, uint64_t length) const;
    bool WritableRange(uint64_t destination, uint64_t length) const;
    bool CopyToUser(uint64_t destination, const void* source, uint64_t length) const;
    bool CopyString(char* destination, uint64_t source, uint64_t capacity) const;
    PageTableManager directory;
private:
    void* AllocatePage();
    PageTable* leaf;
    void* owned[USER_MAX_PAGES + 4];
    uint64_t ownedCount;
    UserAddressSpace(const UserAddressSpace&) = delete;
    UserAddressSpace& operator=(const UserAddressSpace&) = delete;
};
