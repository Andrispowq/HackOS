#pragma once
#include "command.h"

inline bool FindUser(const char* name, HackosUserInfo& user)
{
    for (uint64_t i = 0; syscall_user_info(i, &user) == 1; ++i)
    {
        if (Equal(name, user.name))
        {
            return true;
        }
    }

    Print("Unknown user.\n");
    return false;
}

inline bool FindGroup(const char* name, HackosGroupInfo& group)
{
    for (uint64_t i = 0; syscall_group_info(i, &group) == 1; ++i)
    {
        if (Equal(name, group.name))
        {
            return true;
        }
    }

    Print("Unknown group.\n");
    return false;
}

inline int AccountResult(uint64_t result, const char* success)
{
    if (result == HACKOS_DENIED || result == HACKOS_ERROR)
    {
        Print(result == HACKOS_DENIED ? "Permission denied: root required.\n" : "Account operation failed.\n");
        return 1;
    }

    Print(success);
    Print("\n");
    return 0;
}
