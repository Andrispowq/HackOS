#pragma once
#include "accounts.h"

class AccountIO
{
  public:
    virtual bool Read(unsigned slot, void* buffer) = 0;
    virtual bool Write(unsigned slot, void* buffer) = 0;
    virtual bool Flush() = 0;
};

struct AccountImage
{
    uint64_t magic;
    uint32_t version, length;
    uint64_t generation;
    uint8_t digest[32];
    uint64_t reserved;
    AccountData data;
    uint8_t padding[8192 - 64 - sizeof(AccountData)];
};

static_assert(sizeof(AccountImage) == 8192, "account slot size");

class AccountStore
{
  public:
    enum State
    {
        Broken = 0,
        Empty = 1,
        Ready = 2
    };

    State Load(AccountIO* device, AccountRegistry& registry);
    bool Commit(const AccountRegistry& registry);

    State Status() const
    {
        return state;
    }

    bool Recovered() const
    {
        return recovered;
    }

  private:
    bool Valid(AccountImage& image);
    AccountIO* io = nullptr;
    State state = Broken;
    unsigned active = 1;
    uint64_t generation = 0;
    bool recovered = false;
    AccountImage images[2] = {};
};

extern AccountStore accountStore;
void InitialiseAccountStorage();
