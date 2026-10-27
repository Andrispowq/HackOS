#include "filter_line.h"

struct Line
{
    unsigned offset, length;
};

static char data[128 * 1024];
static Line lines[4096];
static unsigned used, count;
static bool numeric, reverseOrder, insensitive, ignoreBlanks, unique;

static Line Trim(Line line)
{
    while (line.length && Blank(data[line.offset]))
    {
        ++line.offset;
        --line.length;
    }
    return line;
}

static int TextCompare(Line a, Line b, bool fold)
{
    unsigned size = a.length < b.length ? a.length : b.length;
    for (unsigned i = 0; i < size; ++i)
    {
        unsigned char x = data[a.offset + i], y = data[b.offset + i];
        if (fold)
        {
            x = Fold(x);
            y = Fold(y);
        }
        if (x != y)
        {
            return x < y ? -1 : 1;
        }
    }
    return a.length < b.length ? -1 : a.length > b.length ? 1 : 0;
}

struct NumericKey
{
    Line integer, fraction;
    bool negative;
};

static bool Digit(char ch)
{
    return ch >= '0' && ch <= '9';
}

static NumericKey ParseNumber(Line line)
{
    line = Trim(line);
    unsigned end = line.offset + line.length, position = line.offset;
    bool negative = position < end && data[position] == '-';
    position += negative;
    while (position < end && data[position] == '0')
    {
        ++position;
    }
    unsigned start = position;
    while (position < end && Digit(data[position]))
    {
        ++position;
    }
    Line integer = {start, position - start};
    if (position < end && data[position] == '.')
    {
        ++position;
    }
    start = position;
    while (position < end && Digit(data[position]))
    {
        ++position;
    }
    while (position > start && data[position - 1] == '0')
    {
        --position;
    }
    return {integer, {start, position - start}, negative && (integer.length || position != start)};
}

static int NumericCompare(Line a, Line b)
{
    NumericKey x = ParseNumber(a), y = ParseNumber(b);
    if (x.negative != y.negative)
    {
        return x.negative ? -1 : 1;
    }
    int result = x.integer.length < y.integer.length ? -1 : x.integer.length > y.integer.length ? 1 : 0;
    if (!result)
    {
        result = TextCompare(x.integer, y.integer, false);
    }
    if (!result)
    {
        // Missing fractional digits are zeros; no integer/floating-point conversion.
        unsigned size = x.fraction.length > y.fraction.length ? x.fraction.length : y.fraction.length;
        for (unsigned i = 0; i < size; ++i)
        {
            char p = i < x.fraction.length ? data[x.fraction.offset + i] : '0';
            char q = i < y.fraction.length ? data[y.fraction.offset + i] : '0';
            if (p != q)
            {
                result = p < q ? -1 : 1;
                break;
            }
        }
    }
    return x.negative ? -result : result;
}

static int KeyCompare(Line a, Line b)
{
    if (numeric)
    {
        return NumericCompare(a, b);
    }
    return TextCompare(ignoreBlanks ? Trim(a) : a, ignoreBlanks ? Trim(b) : b, insensitive);
}

static bool Less(Line a, Line b)
{
    int result = KeyCompare(a, b);
    if (!result && !unique)
    {
        result = TextCompare(a, b, false);
    }
    // Preserve the first input representative for -u, including under -r.
    return result ? (reverseOrder ? result > 0 : result < 0) : a.offset < b.offset;
}

static void Sift(unsigned root, unsigned size)
{
    while (root * 2 + 1 < size)
    {
        unsigned child = root * 2 + 1;
        if (child + 1 < size && Less(lines[child], lines[child + 1]))
        {
            ++child;
        }
        if (!Less(lines[root], lines[child]))
        {
            break;
        }
        Line temporary = lines[root];
        lines[root] = lines[child];
        lines[child] = temporary;
        root = child;
    }
}

int main(int argc, char** argv)
{
    const char* usage = "Usage: sort [-bfnru] [--] [file ...]\n"
                        "Byte order; -b ignore leading blanks, -f fold ASCII case, -n decimal numbers,\n"
                        "-r reverse, -u unique keys. Limits: 128 KiB including newlines, 4096 lines.\n";
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
            switch (option[j])
            {
            case 'b':
                ignoreBlanks = true;
                break;
            case 'f':
                insensitive = true;
                break;
            case 'n':
                numeric = true;
                break;
            case 'r':
                reverseOrder = true;
                break;
            case 'u':
                unique = true;
                break;
            default:
                return FilterUsage(usage);
            }
        }
    }

    FilterInputs inputs;
    bool ok = true;
    int files = argc - first;
    for (int i = 0; i < (files ? files : 1) && ok; ++i)
    {
        const char* path = files ? argv[first + i] : "-";
        auto input = inputs.Open("sort", path);
        if (!input)
        {
            return 1;
        }
        unsigned start = used;
        int ch;
        while ((ch = input->Next()) >= 0)
        {
            if (used == sizeof(data) || count == sizeof(lines) / sizeof(lines[0]))
            {
                ok = false;
                break;
            }
            data[used++] = ch;
            if (ch == '\n')
            {
                lines[count++] = {start, used - start - 1};
                start = used;
            }
        }
        if (ok && ch == -1 && start != used)
        {
            if (used == sizeof(data))
            {
                ok = false;
            }
            else
            {
                lines[count++] = {start, used - start};
                data[used++] = '\n';
            }
        }
        if (!ok)
        {
            FilterError("sort", path, "input exceeds 128 KiB or 4096 lines");
        }
        ok = inputs.Finish("sort", path, input) && ok;
    }
    if (!ok)
    {
        return 1;
    }

    // Iterative heapsort bounds both worst-case work and stack use.
    for (unsigned i = count / 2; i; --i)
    {
        Sift(i - 1, count);
    }
    for (unsigned size = count; size > 1; --size)
    {
        Line temporary = lines[0];
        lines[0] = lines[size - 1];
        lines[size - 1] = temporary;
        Sift(0, size - 1);
    }
    FilterOutput output;
    for (unsigned i = 0; i < count && !output.error; ++i)
    {
        if (!unique || !i || KeyCompare(lines[i - 1], lines[i]))
        {
            output.Bytes(data + lines[i].offset, lines[i].length);
            output.Byte('\n');
        }
    }
    return output.Finish("sort") ? 0 : 1;
}
