#include "command.h"

int main(int argc, char** argv)
{
    int first = 1;
    bool newline = true;
    while (first < argc && Equal(argv[first], "-n"))
    {
        newline = false;
        ++first;
    }

    for (int i = first; i < argc; ++i)
    {
        if (i > first && !Write(1, " ", 1))
        {
            return 1;
        }
        if (!Write(1, argv[i], Length(argv[i])))
        {
            return 1;
        }
    }

    return newline && !Write(1, "\n", 1) ? 1 : 0;
}
