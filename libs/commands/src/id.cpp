#include "accounts.h"

int main(int argc, char**)
{
#ifdef WHOAMI
    const char* command = "whoami";
#else
    const char* command = "id";
#endif
    if (!Arguments(argc, 1, command))
    {
        return 2;
    }

    HackosCredentials credentials;
    if (syscall_identity(&credentials) != 0)
    {
        return 1;
    }

    HackosUserInfo user;
    const char* name = "unknown";
    for (uint64_t i = 0; syscall_user_info(i, &user) == 1; ++i)
    {
        if (user.credentials.uid == credentials.uid)
        {
            name = user.name;
            break;
        }
    }

#ifdef WHOAMI
    Print(name);
#else
    Print("uid=");
    Number(credentials.uid);
    Print("(");
    Print(name);
    Print(") gid=");
    Number(credentials.gid);
    Print(" groups=");
    Number(credentials.gid);
    for (uint32_t i = 0; i < credentials.groupCount; ++i)
    {
        Print(",");
        Number(credentials.groups[i]);
    }
#endif
    Print("\n");
    return 0;
}
