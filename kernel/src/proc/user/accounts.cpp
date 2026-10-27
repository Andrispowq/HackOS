#include "accounts.h"
#include "lib/string.h"

AccountRegistry accounts;

void AccountRegistry::Initialise()
{
    // Keep the unused bounded tables in BSS rather than the BIOS disk payload.
    // Called once before tasks/syscalls become reachable.
    *this = {};
    users[0] = {{0, 0, 0, 0, {}}, "root"};
    users[1] = {{1000, 1000, 0, 0, {}}, "guest"};
    groups[0] = {0, 0, "root"};
    groups[1] = {1000, 0, "users"};
    userCount = groupCount = 2;
    nextUid = nextGid = 1001;
}

static bool ValidName(const char* name)
{
    if (!name || name[0] < 'a' || name[0] > 'z')
    {
        return false;
    }

    for (unsigned i = 1; i < HACKOS_ACCOUNT_NAME; ++i)
    {
        char c = name[i];
        if (!c)
        {
            return true;
        }

        if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-')
        {
            return false;
        }
    }

    return false;
}

const HackosUserInfo* AccountRegistry::User(uint64_t index) const
{
    return index < userCount ? &users[index] : nullptr;
}

const HackosGroupInfo* AccountRegistry::Group(uint64_t index) const
{
    return index < groupCount ? &groups[index] : nullptr;
}

const HackosCredentials* AccountRegistry::Credentials(uint64_t uid) const
{
    for (unsigned i = 0; i < userCount; ++i)
    {
        if (users[i].credentials.uid == uid)
        {
            return &users[i].credentials;
        }
    }

    return nullptr;
}

bool AccountRegistry::HasGroup(uint64_t gid) const
{
    for (unsigned i = 0; i < groupCount; ++i)
    {
        if (groups[i].gid == gid)
        {
            return true;
        }
    }

    return false;
}

uint64_t AccountRegistry::CreateUser(const char* name, uint64_t gid)
{
    if (!ValidName(name) || !HasGroup(gid) || userCount == 32)
    {
        return HACKOS_ERROR;
    }

    for (unsigned i = 0; i < userCount; ++i)
    {
        if (strcmp(users[i].name, (char*)name) == 0)
        {
            return HACKOS_ERROR;
        }
    }

    auto& user = users[userCount++];
    user.credentials.uid = nextUid++;
    user.credentials.gid = gid;
    strcpy(user.name, name);
    return user.credentials.uid;
}

uint64_t AccountRegistry::CreateGroup(const char* name)
{
    if (!ValidName(name) || groupCount == 32)
    {
        return HACKOS_ERROR;
    }

    for (unsigned i = 0; i < groupCount; ++i)
    {
        if (strcmp(groups[i].name, (char*)name) == 0)
        {
            return HACKOS_ERROR;
        }
    }

    auto& group = groups[groupCount++];
    group.gid = nextGid++;
    strcpy(group.name, name);
    return group.gid;
}

uint64_t AccountRegistry::AddGroup(uint64_t uid, uint64_t gid)
{
    if (!HasGroup(gid))
    {
        return HACKOS_ERROR;
    }

    for (unsigned i = 0; i < userCount; ++i)
    {
        auto& credentials = users[i].credentials;
        if (credentials.uid != uid)
        {
            continue;
        }

        if (credentials.gid == gid)
        {
            return 0;
        }

        for (unsigned j = 0; j < credentials.groupCount; ++j)
        {
            if (credentials.groups[j] == gid)
            {
                return 0;
            }
        }

        if (credentials.groupCount == HACKOS_MAX_GROUPS)
        {
            return HACKOS_ERROR;
        }

        credentials.groups[credentials.groupCount++] = gid;
        return 0;
    }

    return HACKOS_ERROR;
}
