#include "command.h"
#include "hackos/stdio.h"

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

    BufferedOutput output;
    if (longFormat)
    {
        output.Print("MODE         UID   GID ATTR      BYTES NAME\n");
    }

    HackosFileInfo entries[HACKOS_READDIR_BATCH_MAX];
    uint64_t offset = 0, available = 0, next = 0;
    for (;;)
    {
        if (next == available)
        {
            available = syscall_readdir_batch(path, offset, entries, HACKOS_READDIR_BATCH_MAX);
            if (!available)
            {
                break;
            }
            if (available > HACKOS_READDIR_BATCH_MAX)
            {
                output.Flush();
                Error("Cannot list directory.\n");
                return 1;
            }
            offset += available;
            next = 0;
        }
        const HackosFileInfo& info = entries[next++];
        if (!all && (info.name[0] == '.' || (info.attributes & 2)))
        {
            continue;
        }

        bool directory = info.attributes & HACKOS_DIRECTORY;
        if (longFormat)
        {
            char mode[] = "----------";
            mode[0] = directory ? 'd' : '-';
            const char* bits = "rwxrwxrwx";
            for (unsigned bit = 0; bit < 9; ++bit)
            {
                if (info.mode & (1 << (8 - bit)))
                {
                    mode[bit + 1] = bits[bit];
                }
            }

            char attributes[] = "----";
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

            char fields[96];
            snprintf(fields, sizeof(fields), "%s %5u %5u %s %10lu ", mode, info.uid, info.gid, attributes, info.size);
            output.Print(fields);
        }

        output.Print(info.name);
        if (directory)
        {
            output.Print("/");
        }

        output.Print("\n");
    }

    return output.Flush() ? 0 : 1;
}
