#pragma once
#include "hackos/abi.h"

struct PasswordRecord
{
    uint32_t enabled, iterations;
    uint8_t salt[16], digest[32];
};

struct AccountData
{
    uint32_t userCount, groupCount, nextUid, nextGid;
    HackosUserInfo users[32];
    HackosGroupInfo groups[32];
    PasswordRecord passwords[32];
};

static_assert(sizeof(PasswordRecord) == 56 && sizeof(AccountData) == 5648, "account disk schema");

class AccountRegistry
{
  public:
    void Initialise();
    const HackosUserInfo* User(uint64_t index) const;
    const HackosGroupInfo* Group(uint64_t index) const;
    const HackosCredentials* Credentials(uint64_t uid) const;
    uint64_t CreateUser(const char* name, uint64_t gid);
    uint64_t CreateGroup(const char* name);
    uint64_t AddGroup(uint64_t uid, uint64_t gid);
    uint64_t Edit(uint64_t operation, uint64_t id, uint64_t value, const char* name);
    int FindUser(const char* name) const;
    bool Setup(const char* name, const PasswordRecord& password);
    bool SetPassword(uint64_t uid, const PasswordRecord& password);

    const AccountData& Data() const
    {
        return data;
    }

    void Restore(const AccountData& value)
    {
        data = value;
    }

    static bool Validate(const AccountData& value);

  private:
    AccountData data = {};
    bool HasGroup(uint64_t gid) const;
};

extern AccountRegistry accounts;
