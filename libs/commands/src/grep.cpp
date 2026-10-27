#include "grep_pattern.h"

static char line[64 * 1024];

int main(int argc, char** argv)
{
    const char* usage = "Usage: grep [-FinvclqHh] [-e pattern] [--] [pattern] [file ...]\n"
                        "Patterns: literals, . ^ $ * [abc] [a-z] [^abc], escaped metacharacters.\n"
                        "Use -F for literal text. Limit: 128 pattern atoms, 64 KiB per line.\n";
    bool fixed = false, insensitive = false, numbers = false, inverse = false;
    bool counts = false, names = false, quiet = false;
    int headers = 0, first = 1;
    const char* pattern = nullptr;
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
            case 'F':
                fixed = true;
                break;
            case 'i':
                insensitive = true;
                break;
            case 'n':
                numbers = true;
                break;
            case 'v':
                inverse = true;
                break;
            case 'c':
                counts = true;
                break;
            case 'l':
                names = true;
                break;
            case 'q':
                quiet = true;
                break;
            case 'H':
                headers = 1;
                break;
            case 'h':
                headers = -1;
                break;
            case 'e':
            {
                if (pattern)
                {
                    return FilterUsage(usage);
                }
                pattern = option + j + 1;
                if (!*pattern)
                {
                    if (++first == argc)
                    {
                        return FilterUsage(usage);
                    }
                    pattern = argv[first];
                }
                j = Length(option) - 1;
                break;
            }
            default:
                return FilterUsage(usage);
            }
        }
    }
    if (!pattern)
    {
        if (first == argc)
        {
            return FilterUsage(usage);
        }
        pattern = argv[first++];
    }
    GrepPattern matcher;
    if (!matcher.Compile(pattern, fixed, insensitive))
    {
        FilterError("grep", nullptr, "invalid, unsupported or oversized pattern (see --help)");
        return 2;
    }

    FilterInputs inputs;
    FilterOutput output;
    bool ok = true, matched = false;
    int files = argc - first;
    bool prefix = headers == 1 || (!headers && files > 1);
    for (int i = 0; i < (files ? files : 1) && !output.error; ++i)
    {
        const char* path = files ? argv[first + i] : "-";
        const char* label = Equal(path, "-") ? "(standard input)" : path;
        auto input = inputs.Open("grep", path);
        if (!input)
        {
            ok = false;
            continue;
        }
        uint64_t number = 0, selected = 0;
        bool done = false;
        while (!done && !output.error)
        {
            unsigned length = 0;
            int ch;
            while ((ch = input->Next()) >= 0 && ch != '\n')
            {
                if (length == sizeof(line))
                {
                    FilterError("grep", path, "line exceeds 64 KiB");
                    ok = false;
                    done = true;
                    break;
                }
                line[length++] = ch;
            }
            if (done || ch == -2 || (ch == -1 && !length))
            {
                break;
            }
            ++number;
            done = ch == -1;
            if (matcher.Match(line, length) != inverse)
            {
                ++selected;
                matched = true;
                if (quiet)
                {
                    inputs.Finish("grep", path, input);
                    return 0;
                }
                if (names)
                {
                    output.Text(label);
                    output.Byte('\n');
                    break;
                }
                if (!counts)
                {
                    if (prefix)
                    {
                        output.Text(label);
                        output.Byte(':');
                    }
                    if (numbers)
                    {
                        output.Number(number);
                        output.Byte(':');
                    }
                    output.Bytes(line, length);
                    // grep treats a final partial input line as a text line.
                    output.Byte('\n');
                }
            }
        }
        ok = inputs.Finish("grep", path, input) && ok;
        if (counts && !names && !quiet)
        {
            if (prefix)
            {
                output.Text(label);
                output.Byte(':');
            }
            output.Number(selected);
            output.Byte('\n');
        }
    }
    ok = output.Finish("grep") && ok;
    return !ok ? 2 : matched ? 0 : 1;
}
