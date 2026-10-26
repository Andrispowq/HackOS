#include "address_space.h"
#include "lib/memory.h"

extern PageTableManager KernelDirectory;
uint64_t user_pages_live;

void* UserAddressSpace::AllocatePage()
{
    if (ownedCount == USER_MAX_PAGES + 4) return nullptr;
    void* page = PageFrameAllocator::SharedAllocator()->TryRequestPage();
    if (!page) return nullptr;
    memset(page, 0, 4096);
    owned[ownedCount++] = page;
    ++user_pages_live;
    return page;
}

UserAddressSpace::~UserAddressSpace()
{
    // Called on a kernel task's stack after switching away from this CR3.
    while (ownedCount)
    {
        PageFrameAllocator::SharedAllocator()->FreePages(owned[--ownedCount], 1);
        --user_pages_live;
    }
}

static void LinkUserTable(PageTableEntry& entry, PageTable* table)
{
    entry = {};
    entry.address = (uint64_t)table >> 12;
    entry.present = entry.writeable = entry.user_access = 1;
    // Permissions, including NX, are set at leaves; an NX ancestor would
    // also disable execution on executable siblings.
}

bool UserAddressSpace::Initialise()
{
    auto kernel = KernelDirectory.GetPML4();
    if (!user_nx_enabled || kernel->entries[USER_BASE >> 39].present) return false;
    auto root = (PageTable*)AllocatePage();
    auto pdp = (PageTable*)AllocatePage();
    auto pd = (PageTable*)AllocatePage();
    leaf = (PageTable*)AllocatePage();
    if (!root || !pdp || !pd || !leaf) return false;
    memcpy(root, kernel, 4096);
    for (auto& entry : root->entries) entry.user_access = 0;
    LinkUserTable(root->entries[USER_BASE >> 39], pdp);
    LinkUserTable(pdp->entries[0], pd);
    LinkUserTable(pd->entries[0], leaf);
    directory = PageTableManager(root);
    return true;
}

void* UserAddressSpace::AddPage(uint64_t address, bool writable, bool executable)
{
    if (!leaf || address < USER_BASE || address >= USER_END ||
        (address & 4095) || (writable && executable)) return nullptr;
    auto& entry = leaf->entries[(address - USER_BASE) >> 12];
    if (entry.present) return nullptr;
    void* page = AllocatePage();
    if (!page) return nullptr;
    entry.address = (uint64_t)page >> 12;
    entry.present = entry.user_access = 1;
    entry.writeable = writable;
    entry.execution_disabled = !executable;
    return page;
}

bool UserAddressSpace::CopyFromUser(void* destination, uint64_t source, uint64_t length) const
{
    if (!length) return true;
    if (!leaf || source < USER_BASE || source >= USER_END || length > USER_END - source)
        return false;
    // Validate the entire range before copying any bytes. Mapping lifetime is
    // stable: callers run with preemption disabled or in an interrupt gate.
    uint64_t last = (source + length - 1 - USER_BASE) >> 12;
    for (uint64_t i = (source - USER_BASE) >> 12; i <= last; ++i)
        if (!leaf->entries[i].present || !leaf->entries[i].user_access) return false;
    auto output = (uint8_t*)destination;
    while (length)
    {
        const auto& entry = leaf->entries[(source - USER_BASE) >> 12];
        uint64_t offset = source & 4095;
        uint64_t size = 4096 - offset;
        if (size > length) size = length;
        // Use the supervisor identity alias, never an unchecked user pointer.
        memcpy(output, (void*)(((uint64_t)entry.address << 12) + offset), size);
        output += size;
        source += size;
        length -= size;
    }
    return true;
}
