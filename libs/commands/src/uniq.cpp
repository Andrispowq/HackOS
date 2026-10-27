#include "filter_line.h"

static FilterLine buffers[2];
static uint64_t skipFields, skipBytes, width = ~0ULL;
static bool insensitive;

static unsigned KeyStart(const FilterLine& line)
{
    unsigned position = 0;
    for (uint64_t field = 0; field < skipFields && position < line.length; ++field)
    {
        while (position < line.length && Blank(line.data[position]))
        {
            ++position;
        }
        while (position < line.length && !Blank(line.data[position]))
        {
            ++position;
        }
    }
    unsigned remaining = line.length - position;
    return position + (skipBytes < remaining ? skipBytes : remaining);
}

static bool Same(const FilterLine& a, const FilterLine& b)
{
    unsigned x = KeyStart(a), y = KeyStart(b);
    unsigned sizeA = a.length - x, sizeB = b.length - y;
    if (width < sizeA)
    {
        sizeA = width;
    }
    if (width < sizeB)
    {
        sizeB = width;
    }
    if (sizeA != sizeB)
    {
        return false;
    }
    for (unsigned i = 0; i < sizeA; ++i)
    {
        unsigned char p = a.data[x + i], q = b.data[y + i];
        if ((insensitive ? Fold(p) : p) != (insensitive ? Fold(q) : q))
        {
            return false;
        }
    }
    return true;
}

static void Emit(FilterOutput& output, const FilterLine& line, uint64_t count, bool counts, bool duplicates,
                 bool singles)
{
    if ((duplicates && count == 1) || (singles && count != 1))
    {
        return;
    }
    if (counts)
    {
        unsigned digits = 1;
        for (uint64_t n = count; n >= 10; n /= 10)
        {
            ++digits;
        }
        for (; digits < 7; ++digits)
        {
            output.Byte(' ');
        }
        output.Number(count);
        output.Byte(' ');
    }
    output.Bytes(line.data, line.length);
    output.Byte('\n');
}

int main(int argc, char** argv)
{
    const char* usage = "Usage: uniq [-cdui] [-f fields] [-s bytes] [-w bytes] [--] [input [output]]\n"
                        "Adjacent equal lines: -c count, -d repeated only, -u single only, -i ASCII case.\n"
                        "Skip fields then bytes; -w limits comparison. Limit: 64 KiB per line.\n";
    bool counts = false, duplicates = false, singles = false;
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
            switch (flag)
            {
            case 'c':
                counts = true;
                break;
            case 'd':
                duplicates = true;
                break;
            case 'u':
                singles = true;
                break;
            case 'i':
                insensitive = true;
                break;
            case 'f':
            case 's':
            case 'w':
            {
                const char* value = option + j + 1;
                if (!*value)
                {
                    if (++first == argc)
                    {
                        return FilterUsage(usage);
                    }
                    value = argv[first];
                }
                uint64_t number;
                if (!Decimal(value, number))
                {
                    return FilterUsage(usage);
                }
                if (flag == 'f')
                {
                    skipFields = number;
                }
                else if (flag == 's')
                {
                    skipBytes = number;
                }
                else
                {
                    width = number;
                }
                j = Length(option) - 1;
                break;
            }
            default:
                return FilterUsage(usage);
            }
        }
    }
    if (argc - first > 2)
    {
        return FilterUsage(usage);
    }

    const char* path = first < argc ? argv[first] : "-";
    FilterInputs inputs;
    auto input = inputs.Open("uniq", path);
    if (!input)
    {
        return 1;
    }
    FilterOutput output;
    const char* destination = first + 1 < argc ? argv[first + 1] : "-";
    bool closeOutput = !Equal(destination, "-");
    if (closeOutput)
    {
        output.fd = syscall_fd_open(destination, HACKOS_OPEN_WRITE | HACKOS_OPEN_CREATE | HACKOS_OPEN_TRUNCATE);
        if (output.fd >= HACKOS_FD_LIMIT)
        {
            FilterError("uniq", destination, "cannot open");
            inputs.Finish("uniq", path, input);
            return 1;
        }
    }

    FilterLine* previous = &buffers[0];
    FilterLine* next = &buffers[1];
    uint64_t count = 0;
    int result = 0;
    while (!output.error && (result = next->Read(*input, "uniq", path)) > 0)
    {
        if (count && Same(*previous, *next))
        {
            ++count;
        }
        else
        {
            if (count)
            {
                Emit(output, *previous, count, counts, duplicates, singles);
            }
            auto temporary = previous;
            previous = next;
            next = temporary;
            count = 1;
        }
    }
    if (count && result == 0)
    {
        Emit(output, *previous, count, counts, duplicates, singles);
    }
    bool ok = inputs.Finish("uniq", path, input) && result >= 0;
    ok = output.Finish("uniq") && ok;
    if (closeOutput && syscall_fd_close(output.fd))
    {
        FilterError("uniq", destination, "close failed");
        ok = false;
    }
    return ok ? 0 : 1;
}
