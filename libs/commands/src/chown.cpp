#include "accounts.h"

int main(int argc, char** argv)
{
    if (!Arguments(argc, 4, "chown <user> <group> <path>"))
    {
        return 2;
    }

    HackosUserInfo user;
    HackosGroupInfo group;
    if (!FindUser(argv[1], user) || !FindGroup(argv[2], group))
    {
        return 1;
    }

    if (syscall_chown(argv[3], user.credentials.uid, group.gid))
    {
        Print("Cannot change ownership.\n");
        return 1;
    }

    return 0;
}
