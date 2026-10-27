#include "accounts.h"

int main(int argc, char**)
{
    if (!Arguments(argc, 1, "groups"))
    {
        return 2;
    }

    HackosGroupInfo group;
    for (uint64_t i = 0; syscall_group_info(i, &group) == 1; ++i)
    {
        Print(group.name);
        Print(" gid=");
        Number(group.gid);
        Print("\n");
    }

    return 0;
}
