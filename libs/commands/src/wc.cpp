#include "filter.h"

struct Counts
{
    uint64_t lines = 0, words = 0, bytes = 0;
};

static void PrintCounts(FilterOutput& output, const Counts& counts, unsigned flags, const char* name)
{
    bool separator = false;
    const uint64_t values[] = {counts.lines, counts.words, counts.bytes};
    for (unsigned i = 0; i < 3; ++i)
    {
        if (flags & (1U << i))
        {
            if (separator)
            {
                output.Byte(' ');
            }
            output.Number(values[i]);
            separator = true;
        }
    }
    if (name)
    {
        output.Byte(' ');
        output.Text(name);
    }
    output.Byte('\n');
}

int main(int argc, char** argv)
{
    const char* usage = "Usage: wc [-lwc] [--] [file ...]\n";
    unsigned flags = 0;
    int first = 1;
    for (; first < argc; ++first)
    {
        const char* option = argv[first];
        if (Equal(option, "--help"))
        {
            return FilterUsage(usage, true);
        }
        if (Equal(option, "--"))
        {
            ++first;
            break;
        }
        if (option[0] != '-' || !option[1])
        {
            break;
        }
        for (unsigned i = 1; option[i]; ++i)
        {
            if (option[i] == 'l')
            {
                flags |= 1;
            }
            else if (option[i] == 'w')
            {
                flags |= 2;
            }
            else if (option[i] == 'c')
            {
                flags |= 4;
            }
            else
            {
                return FilterUsage(usage);
            }
        }
    }
    if (!flags)
    {
        flags = 7;
    }

    FilterInputs inputs;
    FilterOutput output;
    Counts total;
    bool ok = true;
    int files = argc - first;
    for (int i = 0; i < (files ? files : 1) && !output.error; ++i)
    {
        const char* path = files ? argv[first + i] : "-";
        auto input = inputs.Open("wc", path);
        if (!input)
        {
            ok = false;
            continue;
        }
        Counts counts;
        bool word = false;
        int ch;
        while ((ch = input->Next()) >= 0)
        {
            ++counts.bytes;
            counts.lines += ch == '\n';
            bool nextWord = !Space(ch);
            counts.words += nextWord && !word;
            word = nextWord;
        }
        ok = inputs.Finish("wc", path, input) && ok;
        PrintCounts(output, counts, flags, files ? path : nullptr);
        total.lines += counts.lines;
        total.words += counts.words;
        total.bytes += counts.bytes;
    }
    if (files > 1)
    {
        PrintCounts(output, total, flags, "total");
    }
    return output.Finish("wc") && ok ? 0 : 1;
}
