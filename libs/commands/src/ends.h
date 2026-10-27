#pragma once
#include "filter.h"

struct EndOptions
{
    uint64_t count = 10;
    bool bytes = false, fromStart = false;
    int headers = 0, first = 1;

    bool Parse(int argc, char** argv, bool tail)
    {
        for (; first < argc; ++first)
        {
            const char* option = argv[first];
            if (Equal(option, "--"))
            {
                ++first;
                break;
            }
            if (option[0] != '-' || !option[1])
            {
                break;
            }
            for (unsigned j = 1; option[j]; ++j)
            {
                if (option[j] == 'q')
                {
                    headers = -1;
                }
                else if (option[j] == 'v')
                {
                    headers = 1;
                }
                else if (option[j] == 'n' || option[j] == 'c')
                {
                    bytes = option[j] == 'c';
                    const char* number = option + j + 1;
                    if (!*number)
                    {
                        if (++first == argc)
                        {
                            return false;
                        }
                        number = argv[first];
                    }
                    fromStart = tail && *number == '+';
                    if (fromStart)
                    {
                        ++number;
                    }
                    if (!Decimal(number, count))
                    {
                        return false;
                    }
                    break;
                }
                else
                {
                    return false;
                }
            }
        }
        return true;
    }

    void Header(FilterOutput& output, const char* path, int files, bool& printed) const
    {
        if (headers == 1 || (!headers && files > 1))
        {
            if (printed)
            {
                output.Byte('\n');
            }
            output.Text("==> ");
            output.Text(Equal(path, "-") ? "standard input" : path);
            output.Text(" <==\n");
            printed = true;
        }
    }
};
