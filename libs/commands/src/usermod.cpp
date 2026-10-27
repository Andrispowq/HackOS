#include "accounts.h"

int main(int argc, char** argv)
{
    if (!Arguments(argc, 3, "usermod <user> <supplementary-group>"))
    {
        return 2;
    }

    HackosUserInfo user;
    HackosGroupInfo group;
    if (!FindUser(argv[1], user) || !FindGroup(argv[2], group))
    {
        return 1;
    }

    return AccountResult(syscall_user_add_group(user.credentials.uid, group.gid),
                         "Group added (applies to future runas launches).");
}
