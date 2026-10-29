#include "accounts.h"
#include "hackos/stdio.h"

int main(int argc, char**)
{
    if (!Arguments(argc, 1, "groups"))
    {
        return 2;
    }

    if (printf("%-31s %5s\n", "GROUP", "GID") < 0)
    {
        return 1;
    }

    HackosGroupInfo group;
    for (uint64_t i = 0; syscall_group_info(i, &group) == 1; ++i)
    {
        if (printf("%-31s %5u\n", group.name, group.gid) < 0)
        {
            return 1;
        }
    }

    return 0;
}
