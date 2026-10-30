#pragma once
#include "hackos/runtime/http.h"

namespace Hackos::Html
{
constexpr unsigned Limit = 32768, LinkLimit = 64, UrlLimit = 255;
enum : uint8_t { Bold = 1, Heading = 2, Pre = 4 };

inline bool Space(char c)
{
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f';
}

inline bool Copy(char* out, const char* in, unsigned capacity)
{
    unsigned i = 0;
    while (in[i])
    {
        if (i + 1 >= capacity)
        {
            return false;
        }
        out[i] = in[i];
        ++i;
    }
    out[i] = 0;
    return true;
}

// Resolve HTTP references without decoding percent escapes. Unsupported schemes
// are rejected, never handed to the shell or an external application.
inline bool Resolve(const char* base, const char* reference, char* output)
{
    char joined[1025] = {};
    unsigned used = 0;
    auto append = [&](const char* p, unsigned count)
    {
        if (used + count > 1024)
        {
            return false;
        }
        for (unsigned i = 0; i < count; ++i)
        {
            joined[used++] = p[i];
        }
        joined[used] = 0;
        return true;
    };
    unsigned size = 0;
    while (size <= UrlLimit && reference[size])
    {
        ++size;
    }
    if (size > UrlLimit)
    {
        return false;
    }
    unsigned scheme = 0;
    while (scheme < size && reference[scheme] != ':' && reference[scheme] != '/' &&
           reference[scheme] != '?' && reference[scheme] != '#')
    {
        ++scheme;
    }
    if (scheme < size && reference[scheme] == ':')
    {
        if (!append(reference, size))
        {
            return false;
        }
    }
    else if (size >= 2 && reference[0] == '/' && reference[1] == '/')
    {
        if (!append("http:", 5) || !append(reference, size))
        {
            return false;
        }
    }
    else
    {
        Http::Url parsed;
        if (!Http::ParseUrl(base, parsed))
        {
            return false;
        }
        unsigned authority = 7;
        while (base[authority] && base[authority] != '/' && base[authority] != '?' && base[authority] != '#')
        {
            ++authority;
        }
        if (!append(base, authority))
        {
            return false;
        }
        if (size && reference[0] == '/')
        {
            if (!append(reference, size))
            {
                return false;
            }
        }
        else
        {
            unsigned path = 0, last = 0;
            while (parsed.target[path] && parsed.target[path] != '?')
            {
                if (parsed.target[path] == '/')
                {
                    last = path + 1;
                }
                ++path;
            }
            unsigned count = !size || reference[0] == '#' ? 0 : reference[0] == '?' ? path : last;
            if (!size || reference[0] == '#')
            {
                while (parsed.target[count])
                {
                    ++count;
                }
            }
            if (!append(parsed.target, count) || !append(reference, size))
            {
                return false;
            }
        }
    }
    Http::Url parsed;
    if (!Http::ParseUrl(joined, parsed))
    {
        return false;
    }
    unsigned authority = 7;
    while (joined[authority] && joined[authority] != '/' && joined[authority] != '?' && joined[authority] != '#')
    {
        ++authority;
    }
    unsigned at = 0;
    for (; at < authority && at < UrlLimit; ++at)
    {
        output[at] = joined[at];
    }
    if (at != authority || at == UrlLimit)
    {
        return false;
    }
    unsigned root = at;
    output[at++] = '/';
    unsigned p = 1;
    while (parsed.target[p] && parsed.target[p] != '?')
    {
        unsigned start = p;
        while (parsed.target[p] && parsed.target[p] != '/' && parsed.target[p] != '?')
        {
            ++p;
        }
        unsigned n = p - start;
        bool dot = n == 1 && parsed.target[start] == '.';
        bool parent = n == 2 && parsed.target[start] == '.' && parsed.target[start + 1] == '.';
        if (parent)
        {
            if (at > root + 1)
            {
                --at;
            }
            while (at > root + 1 && output[at - 1] != '/')
            {
                --at;
            }
        }
        else if (!dot)
        {
            if (at + n + (parsed.target[p] == '/') > UrlLimit)
            {
                return false;
            }
            for (unsigned i = start; i < p; ++i)
            {
                output[at++] = parsed.target[i];
            }
            if (parsed.target[p] == '/')
            {
                output[at++] = '/';
            }
        }
        if (parsed.target[p] == '/')
        {
            ++p;
        }
    }
    while (parsed.target[p])
    {
        if (at == UrlLimit)
        {
            return false;
        }
        output[at++] = parsed.target[p++];
    }
    output[at] = 0;
    return true;
}

struct Glyph
{
    char text;
    uint8_t style;
    uint16_t link;
};

struct Document
{
    Glyph glyphs[Limit];
    uint16_t columns[Limit], rows[Limit], first[Limit + 2];
    char links[LinkLimit][UrlLimit + 1];
    unsigned count, linkCount, lineCount, width;
    bool limited;

    void Emit(char c, unsigned style, unsigned link)
    {
        if (count == Limit)
        {
            limited = true;
            return;
        }
        glyphs[count++] = {c, (uint8_t)style, (uint16_t)link};
    }

    // A small forgiving tokenizer, not an HTML5 DOM. Every scan is bounded by
    // the supplied byte length, including comments, attributes and raw text.
    void Load(const char* data, unsigned length)
    {
        count = linkCount = width = 0;
        limited = length > Limit;
        if (length > Limit)
        {
            length = Limit;
        }
        unsigned bold = 0, heading = 0, pre = 0, link = 0, ordered = 0;
        bool whitespace = false;
        char suppressed[16] = {};
        auto block = [&]()
        {
            whitespace = false;
            if (count && glyphs[count - 1].text != '\n')
            {
                Emit('\n', 0, 0);
            }
        };
        for (unsigned at = 0; at < length;)
        {
            if (data[at] == '<')
            {
                if (at + 3 < length && data[at + 1] == '!' && data[at + 2] == '-' && data[at + 3] == '-')
                {
                    at += 4;
                    while (at + 2 < length && !(data[at] == '-' && data[at + 1] == '-' && data[at + 2] == '>'))
                    {
                        ++at;
                    }
                    at = at + 2 < length ? at + 3 : length;
                    continue;
                }
                unsigned end = at + 1;
                char quote = 0;
                while (end < length)
                {
                    char c = data[end];
                    if (quote)
                    {
                        if (c == quote)
                        {
                            quote = 0;
                        }
                    }
                    else if (c == '\'' || c == '"')
                    {
                        quote = c;
                    }
                    else if (c == '>')
                    {
                        break;
                    }
                    ++end;
                }
                if (end == length)
                {
                    break;
                }
                unsigned p = at + 1;
                bool closing = data[p] == '/';
                if (closing)
                {
                    ++p;
                }
                char tag[16] = {};
                unsigned n = 0;
                while (p < end && !Space(data[p]) && data[p] != '/')
                {
                    if (n + 1 < sizeof(tag))
                    {
                        tag[n++] = Http::Lower(data[p]);
                    }
                    ++p;
                }
                at = end + 1;
                if (*suppressed)
                {
                    if (closing && Http::Equal(tag, suppressed))
                    {
                        suppressed[0] = 0;
                    }
                    continue;
                }
                if (Http::Equal(tag, "script") || Http::Equal(tag, "style") || Http::Equal(tag, "head") || Http::Equal(tag, "title"))
                {
                    if (!closing)
                    {
                        Copy(suppressed, tag, sizeof(suppressed));
                    }
                    continue;
                }
                if (Http::Equal(tag, "b") || Http::Equal(tag, "strong"))
                {
                    bold = closing ? (bold ? bold - 1 : 0) : bold + 1;
                }
                bool h = tag[0] == 'h' && tag[1] >= '1' && tag[1] <= '6' && !tag[2];
                if (h || Http::Equal(tag, "p") || Http::Equal(tag, "div") || Http::Equal(tag, "ul") ||
                    Http::Equal(tag, "ol") || Http::Equal(tag, "li") || Http::Equal(tag, "pre") || Http::Equal(tag, "hr"))
                {
                    block();
                    if (count && (count < 2 || glyphs[count - 2].text != '\n') &&
                        (h || Http::Equal(tag, "p") || Http::Equal(tag, "pre")))
                    {
                        Emit('\n', 0, 0);
                    }
                }
                if (h)
                {
                    heading = closing ? 0 : 1;
                }
                if (Http::Equal(tag, "br"))
                {
                    Emit('\n', 0, 0);
                }
                if (Http::Equal(tag, "pre"))
                {
                    pre = !closing;
                }
                if (Http::Equal(tag, "ol"))
                {
                    ordered = closing ? 0 : 1;
                }
                if (Http::Equal(tag, "li") && !closing)
                {
                    if (ordered)
                    {
                        if (ordered >= 10)
                        {
                            Emit('0' + ordered / 10, 0, 0);
                        }
                        Emit('0' + ordered % 10, 0, 0);
                        Emit('.', 0, 0);
                        ordered = ordered == 99 ? 1 : ordered + 1;
                    }
                    else
                    {
                        Emit('-', 0, 0);
                    }
                    Emit(' ', 0, 0);
                }
                if (Http::Equal(tag, "hr") && !closing)
                {
                    for (unsigned i = 0; i < 24; ++i)
                    {
                        Emit('-', 0, 0);
                    }
                    Emit('\n', 0, 0);
                }
                if (Http::Equal(tag, "a"))
                {
                    link = 0;
                    if (!closing)
                    {
                        while (p < end)
                        {
                            while (p < end && (Space(data[p]) || data[p] == '/'))
                            {
                                ++p;
                            }
                            char name[16] = {};
                            n = 0;
                            while (p < end && !Space(data[p]) && data[p] != '=')
                            {
                                if (n + 1 < sizeof(name))
                                {
                                    name[n++] = Http::Lower(data[p]);
                                }
                                ++p;
                            }
                            while (p < end && Space(data[p]))
                            {
                                ++p;
                            }
                            if (p == end || data[p] != '=')
                            {
                                continue;
                            }
                            ++p;
                            while (p < end && Space(data[p]))
                            {
                                ++p;
                            }
                            char delimiter = p < end && (data[p] == '"' || data[p] == '\'') ? data[p++] : 0;
                            unsigned start = p;
                            while (p < end && (delimiter ? data[p] != delimiter : !Space(data[p])))
                            {
                                ++p;
                            }
                            unsigned size = p - start;
                            if (Http::Equal(name, "href") && size <= UrlLimit && linkCount < LinkLimit)
                            {
                                unsigned used = 0;
                                for (unsigned i = start; i < p;)
                                {
                                    links[linkCount][used++] = Entity(data, p, i);
                                }
                                links[linkCount][used] = 0;
                                link = ++linkCount;
                            }
                            if (delimiter && p < end)
                            {
                                ++p;
                            }
                        }
                    }
                }
                continue;
            }
            if (*suppressed)
            {
                ++at;
                continue;
            }
            char c = Entity(data, length, at);
            unsigned style = (bold ? Bold : 0) | (heading ? Bold | Heading : 0) | (pre ? Pre : 0);
            if (pre)
            {
                if (c == '\r')
                {
                    continue;
                }
                if (c == '\t')
                {
                    for (unsigned i = 0; i < 4; ++i)
                    {
                        Emit(' ', style, link);
                    }
                }
                else
                {
                    Emit(c == '\n' || (c >= 32 && c < 127) ? c : '?', style, link);
                }
            }
            else if (Space(c))
            {
                whitespace = count && glyphs[count - 1].text != '\n';
            }
            else
            {
                if (whitespace)
                {
                    Emit(' ', style, link);
                }
                whitespace = false;
                Emit(c >= 32 && c < 127 ? c : '?', style, link);
            }
        }
    }

    static char Entity(const char* data, unsigned end, unsigned& at)
    {
        char c = data[at++];
        if ((uint8_t)c >= 128)
        {
            while (at < end && ((uint8_t)data[at] & 0xc0) == 0x80)
            {
                ++at;
            }
            return '?';
        }
        if (c != '&')
        {
            return c;
        }
        unsigned stop = at;
        while (stop < end && stop - at < 12 && data[stop] != ';' && !Space(data[stop]))
        {
            ++stop;
        }
        if (stop == end || data[stop] != ';')
        {
            return c;
        }
        char name[16] = {};
        for (unsigned i = at; i < stop; ++i)
        {
            name[i - at] = data[i];
        }
        char value = 0;
        if (Http::Equal(name, "amp"))
        {
            value = '&';
        }
        else if (Http::Equal(name, "lt"))
        {
            value = '<';
        }
        else if (Http::Equal(name, "gt"))
        {
            value = '>';
        }
        else if (Http::Equal(name, "quot"))
        {
            value = '"';
        }
        else if (Http::Equal(name, "apos"))
        {
            value = '\'';
        }
        else if (Http::Equal(name, "nbsp"))
        {
            value = ' ';
        }
        else if (name[0] == '#')
        {
            unsigned p = 1, number = 0, base = 10;
            if (name[p] == 'x' || name[p] == 'X')
            {
                base = 16;
                ++p;
            }
            if (!name[p])
            {
                return c;
            }
            for (; name[p]; ++p)
            {
                int digit = Http::Hex(name[p]);
                if (digit < 0 || (unsigned)digit >= base || number > 0x10ffff / base)
                {
                    return c;
                }
                number = number * base + digit;
            }
            value = number >= 32 && number < 127 ? number : '?';
        }
        if (!value)
        {
            return c;
        }
        at = stop + 1;
        return value;
    }

    void Layout(unsigned cols)
    {
        if (!cols)
        {
            cols = 1;
        }
        if (width == cols)
        {
            return;
        }
        width = cols;
        unsigned row = 0, col = 0;
        first[0] = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            auto& g = glyphs[i];
            if (g.text != '\n' && !(g.style & Pre) && g.text != ' ' &&
                (!i || glyphs[i - 1].text == ' ' || glyphs[i - 1].text == '\n'))
            {
                unsigned word = 0;
                while (i + word < count && glyphs[i + word].text != ' ' && glyphs[i + word].text != '\n' && word < cols)
                {
                    ++word;
                }
                if (col && col + word > cols)
                {
                    first[++row] = i;
                    col = 0;
                }
            }
            if (col >= cols && g.text != '\n')
            {
                first[++row] = i;
                col = 0;
            }
            columns[i] = col;
            rows[i] = row;
            if (g.text == '\n')
            {
                first[++row] = i + 1;
                col = 0;
            }
            else
            {
                ++col;
            }
        }
        lineCount = row + 1;
        first[lineCount] = count;
    }
};
}
