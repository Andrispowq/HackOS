#include "command.h"

int main(int argc, char**)
{
    if (!Arguments(argc, 1, "clear")) return 2;
    if (syscall_console(1) == 0) return 0;

    Print("Clear request denied.\n");
    return 1;
}
