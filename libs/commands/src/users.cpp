#include "accounts.h"

int main(int argc, char**)
{
    if (!Arguments(argc, 1, "users"))
    {
        return 2;
    }

    HackosUserInfo user;
    for (uint64_t i = 0; syscall_user_info(i, &user) == 1; ++i)
    {
        Print(user.name);
        Print(" uid=");
        Number(user.credentials.uid);
        Print(" gid=");
        Number(user.credentials.gid);
        Print("\n");
    }

    return 0;
}
