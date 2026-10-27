#include "filter.h"

static bool ClassMember(const char* name, unsigned ch)
{
    bool lower = ch >= 'a' && ch <= 'z', upper = ch >= 'A' && ch <= 'Z';
    bool digit = ch >= '0' && ch <= '9';
    bool graph = ch >= 33 && ch <= 126;
    if (Equal(name, "alnum"))
    {
        return lower || upper || digit;
    }
    if (Equal(name, "alpha"))
    {
        return lower || upper;
    }
    if (Equal(name, "blank"))
    {
        return ch == ' ' || ch == '\t';
    }
    if (Equal(name, "cntrl"))
    {
        return ch < 32 || ch == 127;
    }
    if (Equal(name, "digit"))
    {
        return digit;
    }
    if (Equal(name, "graph"))
    {
        return graph;
    }
    if (Equal(name, "lower"))
    {
        return lower;
    }
    if (Equal(name, "print"))
    {
        return graph || ch == ' ';
    }
    if (Equal(name, "punct"))
    {
        return graph && !lower && !upper && !digit;
    }
    if (Equal(name, "space"))
    {
        return Space(ch);
    }
    if (Equal(name, "upper"))
    {
        return upper;
    }
    if (Equal(name, "xdigit"))
    {
        return digit || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
    }
    return false;
}

static bool Character(const char*& text, unsigned& value)
{
    if (!*text)
    {
        return false;
    }
    value = (unsigned char)*text++;
    if (value != '\\')
    {
        return true;
    }
    if (!*text)
    {
        return false;
    }
    if (*text >= '0' && *text <= '7')
    {
        value = 0;
        for (unsigned i = 0; i < 3 && *text >= '0' && *text <= '7'; ++i)
        {
            value = value * 8 + *text++ - '0';
        }
        return value <= 255;
    }
    switch (*text++)
    {
    case 'a':
        value = '\a';
        break;
    case 'b':
        value = '\b';
        break;
    case 'f':
        value = '\f';
        break;
    case 'n':
        value = '\n';
        break;
    case 'r':
        value = '\r';
        break;
    case 't':
        value = '\t';
        break;
    case 'v':
        value = '\v';
        break;
    default:
        value = (unsigned char)text[-1];
        break;
    }
    return true;
}

struct Set
{
    unsigned char bytes[1024];
    // Case classes must align when used in translation's second operand.
    unsigned char classes[1024];
    unsigned length;

    bool Add(unsigned value, unsigned kind = 0)
    {
        if (length == sizeof(bytes))
        {
            return false;
        }
        classes[length] = kind;
        bytes[length++] = value;
        return true;
    }

    bool Parse(const char* text, bool translatingSecond)
    {
        while (*text)
        {
            if (text[0] == '[' && text[1] == ':')
            {
                text += 2;
                char name[8];
                unsigned size = 0;
                while (*text && *text != ':' && size < sizeof(name) - 1)
                {
                    name[size++] = *text++;
                }
                name[size] = 0;
                if (text[0] != ':' || text[1] != ']')
                {
                    return false;
                }
                text += 2;
                unsigned kind = Equal(name, "lower") ? 1 : Equal(name, "upper") ? 2 : 0;
                if (translatingSecond && !kind)
                {
                    return false;
                }
                unsigned before = length;
                for (unsigned ch = 0; ch < 256; ++ch)
                {
                    if (ClassMember(name, ch) && !Add(ch, kind))
                    {
                        return false;
                    }
                }
                if (length == before)
                {
                    return false;
                }
                continue;
            }
            if (text[0] == '[')
            {
                const char* probe = text + 1;
                unsigned ignored;
                if (*probe == '=' || (Character(probe, ignored) && *probe == '*'))
                {
                    return false; // Equivalence and repetition expressions are not implemented.
                }
            }
            unsigned first, last;
            if (!Character(text, first))
            {
                return false;
            }
            last = first;
            if (*text == '-' && text[1])
            {
                ++text;
                if ((text[0] == '[' && text[1] == ':') || !Character(text, last) || last < first)
                {
                    return false;
                }
            }
            for (unsigned ch = first; ch <= last; ++ch)
            {
                if (!Add(ch))
                {
                    return false;
                }
            }
        }
        return true;
    }
};

static Set firstSet, secondSet;

int main(int argc, char** argv)
{
    const char* usage = "Usage: tr [-cCdst] [--] SET1 [SET2]\n"
                        "Translate stdin; -d delete, -s squeeze repeats, -c/-C complement SET1,\n"
                        "-t truncate translation to SET2 length (otherwise repeat its last byte).\n"
                        "Sets: literals, a-z, octal \\000-\\377, \\a \\b \\f \\n \\r \\t \\v \\\\,\n"
                        "ASCII [:alnum:] [:alpha:] [:blank:] [:cntrl:] [:digit:] [:graph:] [:lower:]\n"
                        "[:print:] [:punct:] [:space:] [:upper:] [:xdigit:]. No [=x=] or [x*n].\n"
                        "Translation SET2 classes must pair opposite case classes in SET1.\n"
                        "Limit: 1024 expanded bytes per set. Input is an unbounded byte stream.\n";
    bool complement = false, remove = false, squeeze = false, truncate = false;
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
            case 'c':
            case 'C':
                complement = true;
                break;
            case 'd':
                remove = true;
                break;
            case 's':
                squeeze = true;
                break;
            case 't':
                truncate = true;
                break;
            default:
                return FilterUsage(usage);
            }
        }
    }
    int operands = argc - first;
    bool translate = !remove && operands == 2;
    if (operands < 1 || operands > 2 || (remove && operands != (squeeze ? 2 : 1)) ||
        (!remove && !squeeze && operands != 2) || (truncate && !translate))
    {
        return FilterUsage(usage);
    }
    if (!firstSet.Parse(argv[first], false) || (operands == 2 && !secondSet.Parse(argv[first + 1], translate)))
    {
        FilterError("tr", nullptr, "invalid, unsupported or oversized set (see --help)");
        return 2;
    }

    bool selected[256] = {}, squeezed[256] = {};
    unsigned char mapping[256];
    for (unsigned i = 0; i < 256; ++i)
    {
        mapping[i] = i;
    }
    for (unsigned i = 0; i < firstSet.length; ++i)
    {
        selected[firstSet.bytes[i]] = true;
    }
    if (complement)
    {
        firstSet.length = 0;
        for (unsigned i = 0; i < 256; ++i)
        {
            selected[i] = !selected[i];
            if (selected[i])
            {
                firstSet.Add(i);
            }
        }
    }
    if (translate)
    {
        if (!secondSet.length && !truncate)
        {
            return FilterUsage(usage);
        }
        for (unsigned i = 0; i < secondSet.length; ++i)
        {
            unsigned kind = secondSet.classes[i];
            if (kind && (i >= firstSet.length || firstSet.classes[i] != 3 - kind))
            {
                FilterError("tr", nullptr, "case classes must be paired at matching positions");
                return 2;
            }
        }
        for (unsigned i = 0; i < firstSet.length; ++i)
        {
            if (i < secondSet.length)
            {
                mapping[firstSet.bytes[i]] = secondSet.bytes[i];
            }
            else if (!truncate)
            {
                mapping[firstSet.bytes[i]] = secondSet.bytes[secondSet.length - 1];
            }
        }
    }
    if (squeeze)
    {
        if (operands == 1)
        {
            for (unsigned i = 0; i < 256; ++i)
            {
                squeezed[i] = selected[i];
            }
        }
        else
        {
            for (unsigned i = 0; i < secondSet.length; ++i)
            {
                squeezed[secondSet.bytes[i]] = true;
            }
        }
    }

    FilterInput input;
    FilterOutput output;
    int ch, previous = -1;
    while (!output.error && (ch = input.Next()) >= 0)
    {
        if (remove && selected[ch])
        {
            continue;
        }
        ch = mapping[ch];
        if (squeeze && squeezed[ch] && previous == ch)
        {
            continue;
        }
        output.Byte(ch);
        previous = ch;
    }
    bool ok = input.Finish("tr", "-", false);
    return output.Finish("tr") && ok ? 0 : 1;
}
