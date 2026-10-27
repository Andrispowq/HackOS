#pragma once
#include "hackos/user.h"

using namespace User;

inline bool Arguments(int argc, int expected, const char* usage)
{
    if (argc == expected)
    {
        return true;
    }

    Print("Usage: ");
    Print(usage);
    Print("\n");
    return false;
}
