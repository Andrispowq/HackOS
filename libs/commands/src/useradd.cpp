#include "accounts.h"

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3)
    {
        Error("Usage: useradd <name> [group]\n");
        return 2;
    }

    HackosGroupInfo group;
    if (!FindGroup(argc == 3 ? argv[2] : "users", group))
    {
        return 1;
    }

    return AccountResult(syscall_user_create(argv[1], group.gid), "User created. Use passwd to enable login.");
}
