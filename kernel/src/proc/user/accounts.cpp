#include "accounts.h"
#include "lib/string.h"

AccountRegistry accounts;

void AccountRegistry::Initialise()
{
    // Defaults for a genuinely empty store. Passwords start disabled.
    *this = {};
    data.users[0] = {{0, 0, 0, 0, {}}, "root"};
    data.users[1] = {{1000, 1000, 0, 0, {}}, "guest"};
    data.groups[0] = {0, 0, "root"};
    data.groups[1] = {1000, 0, "users"};
    data.userCount = data.groupCount = 2;
    data.nextUid = data.nextGid = 1001;
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
    return index < data.userCount ? &data.users[index] : nullptr;
}

const HackosGroupInfo* AccountRegistry::Group(uint64_t index) const
{
    return index < data.groupCount ? &data.groups[index] : nullptr;
}

const HackosCredentials* AccountRegistry::Credentials(uint64_t uid) const
{
    for (unsigned i = 0; i < data.userCount; ++i)
    {
        if (data.users[i].credentials.uid == uid)
        {
            return &data.users[i].credentials;
        }
    }

    return nullptr;
}

bool AccountRegistry::HasGroup(uint64_t gid) const
{
    for (unsigned i = 0; i < data.groupCount; ++i)
    {
        if (data.groups[i].gid == gid)
        {
            return true;
        }
    }

    return false;
}

uint64_t AccountRegistry::CreateUser(const char* name, uint64_t gid)
{
    if (!ValidName(name) || !HasGroup(gid) || data.userCount == 32)
    {
        return HACKOS_ERROR;
    }

    for (unsigned i = 0; i < data.userCount; ++i)
    {
        if (strcmp((char*)data.users[i].name, (char*)name) == 0)
        {
            return HACKOS_ERROR;
        }
    }

    auto& user = data.users[data.userCount++];
    user.credentials.uid = data.nextUid++;
    user.credentials.gid = gid;
    strcpy(user.name, name);
    return user.credentials.uid;
}

uint64_t AccountRegistry::CreateGroup(const char* name)
{
    if (!ValidName(name) || data.groupCount == 32)
    {
        return HACKOS_ERROR;
    }

    for (unsigned i = 0; i < data.groupCount; ++i)
    {
        if (strcmp(data.groups[i].name, (char*)name) == 0)
        {
            return HACKOS_ERROR;
        }
    }

    auto& group = data.groups[data.groupCount++];
    group.gid = data.nextGid++;
    strcpy(group.name, name);
    return group.gid;
}

uint64_t AccountRegistry::AddGroup(uint64_t uid, uint64_t gid)
{
    if (!HasGroup(gid))
    {
        return HACKOS_ERROR;
    }

    for (unsigned i = 0; i < data.userCount; ++i)
    {
        auto& credentials = data.users[i].credentials;
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

int AccountRegistry::FindUser(const char* name) const
{
    for (unsigned i = 0; i < data.userCount; ++i)
    {
        if (strcmp((char*)data.users[i].name, (char*)name) == 0)
        {
            return i;
        }
    }

    return -1;
}

bool AccountRegistry::Setup(const char* name, const PasswordRecord& password)
{
    if (!ValidName(name) || (FindUser(name) > 0) || !password.enabled)
    {
        return false;
    }

    for (auto& c : data.users[0].name)
    {
        c = 0;
    }

    strcpy(data.users[0].name, name);
    data.passwords[0] = password;
    return true;
}

bool AccountRegistry::SetPassword(uint64_t uid, const PasswordRecord& password)
{
    for (unsigned i = 0; i < data.userCount; ++i)
    {
        if (data.users[i].credentials.uid == uid)
        {
            data.passwords[i] = password;
            return true;
        }
    }

    return false;
}

bool AccountRegistry::Validate(const AccountData& value)
{
    if (value.userCount < 2 || value.userCount > 32 || value.groupCount < 2 || value.groupCount > 32 ||
        value.nextUid != 999 + value.userCount || value.nextGid != 999 + value.groupCount ||
        !value.passwords[0].enabled)
    {
        return false;
    }

    for (unsigned i = 0; i < value.groupCount; ++i)
    {
        const auto& group = value.groups[i];
        if (!ValidName(group.name) || group.reserved || group.gid != (i ? 999 + i : 0))
        {
            return false;
        }

        for (unsigned j = 0; j < i; ++j)
        {
            if (strcmp((char*)group.name, (char*)value.groups[j].name) == 0)
            {
                return false;
            }
        }
    }

    auto knownGroup = [&value](uint32_t gid)
    {
        for (unsigned j = 0; j < value.groupCount; ++j)
        {
            if (value.groups[j].gid == gid)
            {
                return true;
            }
        }

        return false;
    };
    for (unsigned i = 0; i < value.userCount; ++i)
    {
        const auto& user = value.users[i];
        const auto& c = user.credentials;
        const auto& password = value.passwords[i];
        if (!ValidName(user.name) || c.reserved || c.uid != (i ? 999 + i : 0) || !knownGroup(c.gid) ||
            c.groupCount > HACKOS_MAX_GROUPS || password.enabled > 1 ||
            (password.enabled && password.iterations != 600000) || (!password.enabled && password.iterations))
        {
            return false;
        }

        for (unsigned j = 0; j < i; ++j)
        {
            if (strcmp((char*)user.name, (char*)value.users[j].name) == 0)
            {
                return false;
            }
        }

        for (unsigned j = 0; j < c.groupCount; ++j)
        {
            if (!knownGroup(c.groups[j]) || c.groups[j] == c.gid)
            {
                return false;
            }

            for (unsigned k = 0; k < j; ++k)
            {
                if (c.groups[j] == c.groups[k])
                {
                    return false;
                }
            }
        }
    }

    return value.users[0].credentials.gid == 0;
}
