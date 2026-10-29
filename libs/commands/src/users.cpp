#include "accounts.h"
#include "hackos/stdio.h"

int main(int argc, char**)
{
    if (!Arguments(argc, 1, "users"))
    {
        return 2;
    }

    if (printf("%-31s %5s %5s\n", "USER", "UID", "GID") < 0)
    {
        return 1;
    }

    HackosUserInfo user;
    for (uint64_t i = 0; syscall_user_info(i, &user) == 1; ++i)
    {
        if (printf("%-31s %5u %5u\n", user.name, user.credentials.uid, user.credentials.gid) < 0)
        {
            return 1;
        }
    }

    return 0;
}
