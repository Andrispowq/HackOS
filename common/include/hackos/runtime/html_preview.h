#pragma once
#include "hackos/runtime/html.h"

namespace Hackos::Html
{
// Streaming input compaction for the text viewer. Hidden raw text never occupies
// document storage. This is deliberately a bounded tokenizer, not an HTML5 DOM.
class Preview
{
    char tag[1025] = {}, raw[16] = {};
    unsigned used = 0, match = 0, dashes = 0;
    bool inTag = false, comment = false, overflow = false, head = false, full = false;
    char quote = 0;

    void Put(char c, char* out, unsigned capacity, unsigned& size)
    {
        if (!full && size < capacity)
        {
            out[size++] = c;
        }
        else
        {
            limited = full = true;
        }
    }

    void Tag(char* out, unsigned capacity, unsigned& size)
    {
        tag[used] = 0;
        unsigned at = 1;
        bool closing = tag[at] == '/';
        if (closing)
        {
            ++at;
        }
        char name[16] = {};
        unsigned n = 0;
        while (at < used && !Space(tag[at]) && tag[at] != '>' && tag[at] != '/')
        {
            if (n + 1 < sizeof(name))
            {
                name[n++] = Http::Lower(tag[at]);
            }
            ++at;
        }
        if (Http::Equal(name, "head"))
        {
            head = !closing;
            return;
        }
        if (!closing && (Http::Equal(name, "script") || Http::Equal(name, "style") || Http::Equal(name, "title")))
        {
            Copy(raw, name, sizeof(raw));
            match = 0;
            return;
        }
        if (head)
        {
            return;
        }
        if (overflow)
        {
            limited = true;
            return;
        }
        bool keep = Http::Equal(name, "a") || Http::Equal(name, "b") || Http::Equal(name, "strong") ||
                    Http::Equal(name, "p") || Http::Equal(name, "div") || Http::Equal(name, "ul") ||
                    Http::Equal(name, "ol") || Http::Equal(name, "li") || Http::Equal(name, "pre") ||
                    Http::Equal(name, "hr") || Http::Equal(name, "br") ||
                    (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]);
        char compact[1025];
        unsigned count = 0;
        compact[count++] = '<';
        if (closing)
        {
            compact[count++] = '/';
        }
        for (unsigned j = 0; name[j]; ++j)
        {
            compact[count++] = name[j];
        }
        bool anchor = false;
        while (!closing && at < used && tag[at] != '>')
        {
            while (at < used && (Space(tag[at]) || tag[at] == '/'))
            {
                ++at;
            }
            unsigned start = at;
            while (at < used && !Space(tag[at]) && tag[at] != '=' && tag[at] != '>')
            {
                ++at;
            }
            unsigned end = at;
            while (at < used && Space(tag[at]))
            {
                ++at;
            }
            if (at >= used || tag[at] != '=')
            {
                if (at == start)
                {
                    ++at;
                }
                continue;
            }
            ++at;
            while (at < used && Space(tag[at]))
            {
                ++at;
            }
            char q = at < used && (tag[at] == '\'' || tag[at] == '"') ? tag[at++] : 0;
            unsigned value = at;
            while (at < used && (q ? tag[at] != q : !Space(tag[at]) && tag[at] != '>'))
            {
                ++at;
            }
            unsigned last = at;
            if (q && at < used)
            {
                ++at;
            }
            char attribute[8] = {};
            if (end - start >= sizeof(attribute))
            {
                continue;
            }
            for (unsigned j = start; j < end; ++j)
            {
                attribute[j - start] = Http::Lower(tag[j]);
            }
            bool identity = Http::Equal(attribute, "id") || Http::Equal(attribute, "name");
            if (!identity && !(Http::Equal(name, "a") && Http::Equal(attribute, "href")))
            {
                continue;
            }
            anchor = anchor || identity;
            unsigned extra = end - start + last - value + 4;
            if (count + extra + 1 > sizeof(compact))
            {
                limited = true;
                continue;
            }
            compact[count++] = ' ';
            for (unsigned j = start; j < end; ++j)
            {
                compact[count++] = tag[j];
            }
            compact[count++] = '=';
            compact[count++] = q ? q : '"';
            for (unsigned j = value; j < last; ++j)
            {
                compact[count++] = tag[j];
            }
            compact[count++] = q ? q : '"';
        }
        if (!keep && !anchor)
        {
            return;
        }
        compact[count++] = '>';
        if (full || count > capacity - size)
        {
            limited = full = true;
            return;
        }
        for (unsigned i = 0; i < count; ++i)
        {
            Put(compact[i], out, capacity, size);
        }
    }

  public:
    bool limited = false;

    void Feed(const uint8_t* bytes, unsigned length, char* out, unsigned capacity, unsigned& size, bool plain = false)
    {
        for (unsigned i = 0; i < length; ++i)
        {
            char c = (char)bytes[i];
            if (plain)
            {
                Put(c, out, capacity, size);
                continue;
            }
            if (*raw)
            {
                unsigned n = 0;
                while (raw[n])
                {
                    ++n;
                }
                char expected = match == 0 ? '<' : match == 1 ? '/' : match < n + 2 ? raw[match - 2] : '>';
                if (match == n + 2 && Space(c))
                {
                    continue;
                }
                if (Http::Lower(c) == expected)
                {
                    if (++match == n + 3)
                    {
                        raw[0] = 0;
                        match = 0;
                    }
                }
                else
                {
                    match = c == '<' ? 1 : 0;
                }
                continue;
            }
            if (comment)
            {
                if (c == '>' && dashes >= 2)
                {
                    comment = false;
                    dashes = 0;
                }
                else
                {
                    dashes = c == '-' ? (dashes < 2 ? dashes + 1 : 2) : 0;
                }
                continue;
            }
            if (!inTag)
            {
                if (c != '<')
                {
                    if (!head)
                    {
                        Put(c, out, capacity, size);
                    }
                    continue;
                }
                inTag = true;
                used = 0;
                quote = 0;
                overflow = false;
            }
            if (used < sizeof(tag) - 1)
            {
                tag[used++] = c;
            }
            else
            {
                overflow = true;
            }
            if (used == 4 && tag[0] == '<' && tag[1] == '!' && tag[2] == '-' && tag[3] == '-')
            {
                comment = true;
                inTag = false;
                dashes = 0;
            }
            else if (quote)
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
                Tag(out, capacity, size);
                inTag = false;
            }
        }
    }
};
}
