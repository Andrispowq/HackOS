#pragma once
#include "hackos/abi.h"

// Boot-session registry. IDs are stable and never reused within a boot.
// Persistence and authentication deliberately have no implementation here.
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

  private:
    HackosUserInfo users[32] = {};
    HackosGroupInfo groups[32] = {};
    uint32_t userCount = 0, groupCount = 0;
    uint32_t nextUid = 0, nextGid = 0;
    bool HasGroup(uint64_t gid) const;
};

extern AccountRegistry accounts;
