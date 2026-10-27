#include "accounts.h"

int main(int argc, char** argv)
{
    if (!Arguments(argc, 2, "groupadd <name>"))
    {
        return 2;
    }

    return AccountResult(syscall_group_create(argv[1]), "Group created (this boot only).");
}
