#include "filter_line.h"

struct Range
{
    uint64_t first, last;
};

static Range ranges[128];
static unsigned rangeCount;
static FilterLine line;

static bool Position(const char*& text, uint64_t& value)
{
    value = 0;
    const char* start = text;
    while (*text >= '0' && *text <= '9')
    {
        if (value > (~0ULL - (*text - '0')) / 10)
        {
            return false;
        }
        value = value * 10 + *text++ - '0';
    }
    return text != start && value;
}

static bool ParseList(const char* text)
{
    do
    {
        Range range = {1, ~0ULL};
        bool hasStart = *text != '-';
        if (hasStart && !Position(text, range.first))
        {
            return false;
        }
        if (*text == '-')
        {
            ++text;
            if (*text >= '0' && *text <= '9')
            {
                if (!Position(text, range.last))
                {
                    return false;
                }
            }
            else if (!hasStart)
            {
                return false;
            }
        }
        else
        {
            range.last = range.first;
        }
        if (range.last < range.first || rangeCount == sizeof(ranges) / sizeof(ranges[0]))
        {
            return false;
        }
        ranges[rangeCount++] = range;
        if (!*text)
        {
            return true;
        }
        if (*text++ != ',')
        {
            return false;
        }
    } while (*text);
    return false;
}

static bool Selected(uint64_t position)
{
    for (unsigned i = 0; i < rangeCount; ++i)
    {
        if (position >= ranges[i].first && position <= ranges[i].last)
        {
            return true;
        }
    }
    return false;
}

int main(int argc, char** argv)
{
    const char* usage = "Usage: cut (-b LIST | -c LIST | -f LIST) [-d delimiter] [-s] [--] [file ...]\n"
                        "LIST: N,N-M,N-,-M (one-based); -b/-c select bytes, -f selects fields.\n"
                        "Fields default to TAB; -s skips lines without a delimiter.\n"
                        "Limits: 128 ranges, 64 KiB per line; ASCII/byte characters only.\n";
    char mode = 0;
    unsigned char delimiter = '\t';
    bool suppress = false, customDelimiter = false;
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
        for (unsigned j = 1; option[j]; ++j)
        {
            char flag = option[j];
            if (flag == 's')
            {
                suppress = true;
                continue;
            }
            if (flag != 'b' && flag != 'c' && flag != 'f' && flag != 'd')
            {
                return FilterUsage(usage);
            }
            const char* value = option + j + 1;
            if (!*value)
            {
                if (++first == argc)
                {
                    return FilterUsage(usage);
                }
                value = argv[first];
            }
            if (flag == 'd')
            {
                if (Length(value) > 1)
                {
                    return FilterUsage(usage);
                }
                delimiter = *value;
                customDelimiter = true;
            }
            else
            {
                if (mode || !ParseList(value))
                {
                    return FilterUsage(usage);
                }
                mode = flag;
            }
            break;
        }
    }
    if (!mode || (mode != 'f' && (suppress || customDelimiter)))
    {
        return FilterUsage(usage);
    }

    FilterInputs inputs;
    FilterOutput output;
    bool ok = true;
    int files = argc - first;
    for (int i = 0; i < (files ? files : 1) && !output.error; ++i)
    {
        const char* path = files ? argv[first + i] : "-";
        auto input = inputs.Open("cut", path);
        if (!input)
        {
            ok = false;
            continue;
        }
        int result = 0;
        while (!output.error && (result = line.Read(*input, "cut", path)) > 0)
        {
            if (mode == 'f')
            {
                bool delimited = false;
                for (unsigned j = 0; j < line.length; ++j)
                {
                    delimited = delimited || (unsigned char)line.data[j] == delimiter;
                }
                if (!delimited)
                {
                    if (!suppress)
                    {
                        output.Bytes(line.data, line.length);
                        output.Byte('\n');
                    }
                    continue;
                }
                unsigned start = 0, field = 1;
                bool printed = false;
                for (unsigned j = 0; j <= line.length; ++j)
                {
                    if (j == line.length || (unsigned char)line.data[j] == delimiter)
                    {
                        if (Selected(field))
                        {
                            if (printed)
                            {
                                output.Byte(delimiter);
                            }
                            output.Bytes(line.data + start, j - start);
                            printed = true;
                        }
                        ++field;
                        start = j + 1;
                    }
                }
            }
            else
            {
                for (unsigned j = 0; j < line.length; ++j)
                {
                    if (Selected(j + 1))
                    {
                        output.Byte(line.data[j]);
                    }
                }
            }
            output.Byte('\n');
        }
        ok = inputs.Finish("cut", path, input) && result >= 0 && ok;
    }
    return output.Finish("cut") && ok ? 0 : 1;
}
