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
    if (!Arguments(argc, 1, name)) return 2;
    if (syscall_power(operation) == 0) return 0;

    Print("Power request failed.\n");
    return 1;
}
