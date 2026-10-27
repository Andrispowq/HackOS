#include "ends.h"

int main(int argc, char** argv)
{
    const char* usage = "Usage: head [-n N | -c N] [-qv] [--] [file ...]\n";
    if (argc == 2 && Equal(argv[1], "--help"))
    {
        return FilterUsage(usage, true);
    }
    EndOptions options;
    if (!options.Parse(argc, argv, false))
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
        auto input = inputs.Open("head", path);
        if (!input)
        {
            ok = false;
            continue;
        }
        options.Header(output, path, files, printed);
        uint64_t remaining = options.count;
        int ch;
        while (remaining && !output.error && (ch = input->Next()) >= 0)
        {
            output.Byte(ch);
            remaining -= options.bytes || ch == '\n';
        }
        ok = inputs.Finish("head", path, input) && ok;
    }
    return output.Finish("head") && ok ? 0 : 1;
}
