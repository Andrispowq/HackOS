#include "command.h"

static void Usage()
{
    Error("Usage: ls [-l] [-a] [--] [path]\n");
}

int main(int argc, char** argv)
{
    bool longFormat = false;
    bool all = false;
    bool options = true;
    const char* path = ".";
    bool havePath = false;

    for (int i = 1; i < argc; i++)
    {
        const char* arg = argv[i];
        if (options && Equal(arg, "--"))
        {
            options = false;
        }
        else if (options && arg[0] == '-' && arg[1])
        {
            for (unsigned j = 1; arg[j]; j++)
            {
                if (arg[j] == 'l')
                {
                    longFormat = true;
                }
                else if (arg[j] == 'a')
                {
                    all = true;
                }
                else
                {
                    Usage();
                    return 2;
                }
            }
        }
        else
        {
            if (havePath || !*arg)
            {
                Usage();
                return 2;
            }

            path = arg;
            havePath = true;
        }
    }

    if (longFormat)
    {
        Print("MODE       UID GID ATTR       BYTES NAME\n");
    }

    HackosFileInfo info;
    for (uint64_t i = 0;; i++)
    {
        uint64_t result = syscall_readdir(path, i, &info);
        if (result == 0)
        {
            break;
        }

        if (result == HACKOS_ERROR || result == HACKOS_DENIED)
        {
            Error("Cannot list directory.\n");
            return 1;
        }

        if (!all && (info.name[0] == '.' || (info.attributes & 2)))
        {
            continue;
        }

        bool directory = info.attributes & HACKOS_DIRECTORY;
        if (longFormat)
        {
            char mode[] = "---------- ";
            mode[0] = directory ? 'd' : '-';
            const char* bits = "rwxrwxrwx";
            for (unsigned bit = 0; bit < 9; ++bit)
            {
                if (info.mode & (1 << (8 - bit)))
                {
                    mode[bit + 1] = bits[bit];
                }
            }

            Print(mode);
            Number(info.uid);
            Print(" ");
            Number(info.gid);
            Print(" ");
            char attributes[] = "---- ";
            if (info.attributes & 1)
            {
                attributes[0] = 'r';
            }

            if (info.attributes & 2)
            {
                attributes[1] = 'h';
            }

            if (info.attributes & 4)
            {
                attributes[2] = 's';
            }

            if (info.attributes & 32)
            {
                attributes[3] = 'a';
            }

            Print(attributes);

            uint64_t value = info.size;
            unsigned digits = 1;
            while (value >= 10)
            {
                value /= 10;
                digits++;
            }

            for (unsigned padding = digits; padding < 10; padding++)
            {
                Print(" ");
            }

            Number(info.size);
            Print(" ");
        }

        Print(info.name);
        if (directory)
        {
            Print("/");
        }

        Print("\n");
    }

    return 0;
}
