#include "account_store.h"
#include "fs/filesystem.h"

class DiskAccounts : public AccountIO
{
  public:
    uint64_t sectors[2][16] = {};
    Device* disk = nullptr;

    bool Read(unsigned slot, void* buffer) override
    {
        if (!disk || slot > 1)
        {
            return false;
        }

        for (unsigned i = 0; i < 16; ++i)
        {
            if (!disk->Read(sectors[slot][i], (uint8_t*)buffer + i * 512, 1))
            {
                return false;
            }
        }

        return true;
    }

    bool Write(unsigned slot, void* buffer) override
    {
        if (!disk || slot > 1)
        {
            return false;
        }

        for (unsigned i = 0; i < 16; ++i)
        {
            if (!disk->Write(sectors[slot][i], (uint8_t*)buffer + i * 512, 1))
            {
                return false;
            }
        }

        return true;
    }

    bool Flush() override
    {
        return disk && disk->Flush();
    }
};

static DiskAccounts diskAccounts;

void InitialiseAccountStorage()
{
    if (filesystems[0] && filesystems[0]->AccountStorage(diskAccounts.sectors, diskAccounts.disk))
    {
        accountStore.Load(&diskAccounts, accounts);
    }
}
