#include "command.h"

int main(int argc, char**)
{
#ifdef RESTART
    const char* name = "restart";
    uint64_t operation = 1;
#else
    const char* name = "shutdown";
    uint64_t operation = 0;
#endif
    if (!Arguments(argc, 1, name))
    {
        return 2;
    }

    uint64_t result = syscall_power(operation);
    if (result == 0)
    {
        return 0;
    }

    Print(result == HACKOS_DENIED ? "Permission denied: root required.\n" : "Power request failed.\n");
    return 1;
}
