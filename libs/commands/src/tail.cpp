#include "ends.h"

// Retain only the suffix. Input can be arbitrarily long; diagnose a requested
// suffix that no longer fits instead of silently returning a truncated line.
static char suffix[128 * 1024];

static bool Tail(FilterInput& input, FilterOutput& output, const EndOptions& options, const char* path)
{
    if (options.fromStart)
    {
        uint64_t skip = options.count ? options.count - 1 : 0;
        int ch;
        while (!output.error && (ch = input.Next()) >= 0)
        {
            if (!skip)
            {
                output.Byte(ch);
            }
            else if (options.bytes || ch == '\n')
            {
                --skip;
            }
        }
        return true;
    }

    unsigned end = 0, size = 0;
    bool lost = false;
    char preceding = '\n';
    int ch;
    while ((ch = input.Next()) >= 0)
    {
        if (!options.count)
        {
            continue;
        }
        if (size == sizeof(suffix))
        {
            lost = true;
            preceding = suffix[end];
        }
        else
        {
            ++size;
        }
        suffix[end] = ch;
        end = (end + 1) % sizeof(suffix);
    }
    if (input.failed || !size)
    {
        return !input.failed;
    }
    unsigned keep = size;
    if (options.bytes)
    {
        if (options.count < keep)
        {
            keep = options.count;
        }
    }
    else
    {
        uint64_t lines = 0;
        for (unsigned back = 1; back <= size; ++back)
        {
            if (suffix[(end + sizeof(suffix) - back) % sizeof(suffix)] == '\n' && back != 1)
            {
                if (++lines == options.count)
                {
                    keep = back - 1;
                    break;
                }
            }
        }
        // When the retained bytes begin exactly on a line boundary, the
        // preceding newline can supply the last boundary needed by the scan.
        if (keep == size && lost && !(preceding == '\n' && lines + 1 == options.count))
        {
            FilterError("tail", path, "requested suffix exceeds 128 KiB");
            return false;
        }
    }
    if (options.bytes && lost && options.count > sizeof(suffix))
    {
        FilterError("tail", path, "requested suffix exceeds 128 KiB");
        return false;
    }
    unsigned begin = (end + sizeof(suffix) - keep) % sizeof(suffix);
    for (unsigned i = 0; i < keep && !output.error; ++i)
    {
        output.Byte(suffix[(begin + i) % sizeof(suffix)]);
    }
    return true;
}

int main(int argc, char** argv)
{
    const char* usage = "Usage: tail [-n [+]N | -c [+]N] [-qv] [--] [file ...]\n";
    if (argc == 2 && Equal(argv[1], "--help"))
    {
        return FilterUsage(usage, true);
    }
    EndOptions options;
    if (!options.Parse(argc, argv, true))
    {
        return FilterUsage(usage);
    }
    FilterInputs inputs;
    FilterOutput output;
    bool ok = true, printed = false;
    int files = argc - options.first;
    for (int i = 0; i < (files ? files : 1) && !output.error; ++i)
    {
        const char* path = files ? argv[options.first + i] : "-";
        auto input = inputs.Open("tail", path);
        if (!input)
        {
            ok = false;
            continue;
        }
        options.Header(output, path, files, printed);
        ok = Tail(*input, output, options, path) && ok;
        ok = inputs.Finish("tail", path, input) && ok;
    }
    return output.Finish("tail") && ok ? 0 : 1;
}
