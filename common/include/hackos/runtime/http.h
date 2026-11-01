#pragma once
#include "hackos/types.h"
#include "hackos/runtime/ipv4.h"
#include "hackos/runtime/hostname.h"

namespace Hackos::Http
{
constexpr unsigned BodyLimit = 32768, HeaderLimit = 8192, WireLimit = 262144, StreamingLimit = 4 * 1024 * 1024;

inline char Lower(char c)
{
    return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
}

inline bool Equal(const char* a, const char* b)
{
    while (*a && *b)
    {
        if (Lower(*a++) != Lower(*b++))
        {
            return false;
        }
    }
    return *a == *b;
}

inline int Hex(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    c = Lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

inline bool Token(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
    {
        return true;
    }
    const char* punctuation = "!#$%&'*+-.^_`|~";
    for (unsigned i = 0; punctuation[i]; ++i)
    {
        if (c == punctuation[i])
        {
            return true;
        }
    }
    return false;
}

struct Url
{
    char host[256], target[1025];
    uint16_t port;
    bool secure;
};

inline bool ParseUrl(const char* text, Url& url)
{
    if (!text)
    {
        return false;
    }
    unsigned length = 0;
    while (length <= 1024 && text[length])
    {
        ++length;
    }
    if (length < 8 || length > 1024)
    {
        return false;
    }
    bool secure = length >= 8 && Lower(text[4]) == 's';
    unsigned prefix = secure ? 8 : 7;
    const char* scheme = secure ? "https://" : "http://";
    for (unsigned i = 0; i < prefix; ++i)
    {
        if (Lower(text[i]) != scheme[i])
        {
            return false;
        }
    }
    for (unsigned i = prefix; i < length; ++i)
    {
        if ((uint8_t)text[i] <= 32 || (uint8_t)text[i] >= 127 || text[i] == '\\')
        {
            return false;
        }
        if (text[i] == '%' && (i + 2 >= length || Hex(text[i + 1]) < 0 || Hex(text[i + 2]) < 0))
        {
            return false;
        }
    }
    unsigned at = prefix, hostSize = 0;
    char host[256] = {};
    while (at < length && text[at] != ':' && text[at] != '/' && text[at] != '?' && text[at] != '#')
    {
        if (hostSize >= 253)
        {
            return false;
        }
        host[hostSize++] = text[at++];
    }
    if (!hostSize)
    {
        return false;
    }
    url = {};
    uint8_t address[4];
    if (ParseIPv4(host, address))
    {
        for (unsigned i = 0; i <= hostSize; ++i)
        {
            url.host[i] = host[i];
        }
    }
    else if (!NormalizeHostname(host, url.host))
    {
        return false;
    }
    url.secure = secure;
    url.port = secure ? 443 : 80;
    if (at < length && text[at] == ':')
    {
        ++at;
        unsigned port = 0, digits = 0;
        while (at < length && text[at] >= '0' && text[at] <= '9')
        {
            port = port * 10 + text[at++] - '0';
            if (++digits > 5 || port > 65535)
            {
                return false;
            }
        }
        if (!digits || !port)
        {
            return false;
        }
        url.port = port;
    }
    if (at < length && text[at] != '/' && text[at] != '?' && text[at] != '#')
    {
        return false;
    }
    unsigned used = 0;
    if (at == length || text[at] != '/')
    {
        url.target[used++] = '/';
    }
    while (at < length && text[at] != '#')
    {
        url.target[used++] = text[at++];
    }
    url.target[used] = 0;
    return true;
}

// Resolve HTTP(S) references without decoding percent escapes. Unsupported schemes
// are rejected, never handed to the shell or an external application.
inline bool Resolve(const char* base, const char* reference, char* output, unsigned limit = 1024)
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
    while (size <= limit && reference[size])
    {
        ++size;
    }
    if (size > limit)
    {
        return false;
    }
    unsigned scheme = 0;
    while (scheme < size && reference[scheme] != ':' && reference[scheme] != '/' && reference[scheme] != '?' &&
           reference[scheme] != '#')
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
        Url baseUrl;
        if (!ParseUrl(base, baseUrl)) return false;
        if (!append(baseUrl.secure ? "https:" : "http:", baseUrl.secure ? 6 : 5) || !append(reference, size))
        {
            return false;
        }
    }
    else
    {
        Url parsed;
        if (!ParseUrl(base, parsed))
        {
            return false;
        }
        unsigned authority = parsed.secure ? 8 : 7;
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
    Url parsed;
    if (!ParseUrl(joined, parsed))
    {
        return false;
    }
    unsigned authority = parsed.secure ? 8 : 7;
    while (joined[authority] && joined[authority] != '/' && joined[authority] != '?' && joined[authority] != '#')
    {
        ++authority;
    }
    unsigned at = 0;
    for (; at < authority && at < limit; ++at)
    {
        output[at] = joined[at];
    }
    if (at != authority || at == limit)
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
            if (at + n + (parsed.target[p] == '/') > limit)
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
        if (at == limit)
        {
            return false;
        }
        output[at++] = parsed.target[p++];
    }
    unsigned fragment = 0;
    while (joined[fragment] && joined[fragment] != '#')
    {
        ++fragment;
    }
    while (joined[fragment])
    {
        if (at == limit)
        {
            return false;
        }
        output[at++] = joined[fragment++];
    }
    output[at] = 0;
    return true;
}

inline bool RedirectStatus(unsigned status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

// Five redirects, exact transport-tuple loop detection (fragments are not sent).
struct Redirects
{
    char current[1025] = {};
    Url visited[6] = {};
    unsigned count = 0;
    const char* error = nullptr;

    bool Add(const char* address)
    {
        Url parsed;
        if (!ParseUrl(address, parsed))
        {
            error = "invalid redirect URL (use HTTP or HTTPS)";
            return false;
        }
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned j = 0;
            while (parsed.target[j] && parsed.target[j] == visited[i].target[j])
            {
                ++j;
            }
            if (Equal(parsed.host, visited[i].host) && parsed.port == visited[i].port && parsed.secure == visited[i].secure &&
                parsed.target[j] == visited[i].target[j])
            {
                error = "redirect loop";
                return false;
            }
        }
        if (count == 6)
        {
            error = "redirect limit exceeded (five hops)";
            return false;
        }
        visited[count++] = parsed;
        unsigned i = 0;
        do
        {
            current[i] = address[i];
        } while (address[i++]);
        return true;
    }

    bool Next(const char* location)
    {
        char next[1025];
        if (!*location || !Resolve(current, location, next))
        {
            error = "missing or invalid redirect Location (use HTTP or HTTPS)";
            return false;
        }
        // A redirect without its own fragment inherits the original fragment.
        unsigned i = 0, f = 0;
        while (next[i] && next[i] != '#')
        {
            ++i;
        }
        while (current[f] && current[f] != '#')
        {
            ++f;
        }
        if (!next[i])
        {
            while (current[f])
            {
                if (i == 1024)
                {
                    error = "redirect URL too long";
                    return false;
                }
                next[i++] = current[f++];
            }
            next[i] = 0;
        }
        Url previous, destination;
        if (!ParseUrl(current, previous) || !ParseUrl(next, destination)) return false;
        if (previous.secure && !destination.secure)
        {
            error = "HTTPS to HTTP redirect refused";
            return false;
        }
        return Add(next);
    }
};

// Streaming HTTP/1.x response decoder. Callbacks receive only final headers and
// decoded body bytes. No allocation; arbitrary TCP segmentation is supported.
class Decoder
{
  public:
    using Body = bool (*)(void*, const uint8_t*, unsigned);
    using Headers = bool (*)(void*, unsigned, const char*);
    Decoder(Body body, Headers headers, void* context, unsigned limit = BodyLimit)
        : body(body), headers(headers), context(context), bodyLimit(limit > StreamingLimit ? StreamingLimit : limit) {}
    unsigned Status() const { return status; }
    unsigned Received() const { return received; }
    bool PlainText() const { return plainText; }
    bool Done() const { return phase == Complete; }
    const char* Error() const { return error; }

    bool Feed(const uint8_t* bytes, unsigned size)
    {
        if (error || (size && !bytes)) return false;
        unsigned wireLimit = bodyLimit > BodyLimit ? bodyLimit + WireLimit : WireLimit;
        if (size > wireLimit - wire) return Fail("response wire limit exceeded");
        wire += size;
        unsigned at = 0;
        while (at < size)
        {
            if (phase == Complete) return Fail("bytes after response end");
            if (phase == Fixed || phase == Close || phase == ChunkData)
            {
                unsigned count = size - at;
                if (phase != Close && count > remaining) count = remaining;
                if (count > bodyLimit - received || (status == 205 && count)) return Fail("response body limit or status violated");
                if (count && body && !body(context, bytes + at, count)) return Fail("output failed");
                received += count;
                at += count;
                if (phase != Close)
                {
                    remaining -= count;
                    if (!remaining) phase = phase == Fixed ? Complete : ChunkCR;
                }
                continue;
            }
            char c = bytes[at++];
            if (phase == ChunkCR || phase == ChunkLF)
            {
                if (c != (phase == ChunkCR ? '\r' : '\n')) return Fail("invalid chunk terminator");
                phase = phase == ChunkCR ? ChunkLF : ChunkSize;
                continue;
            }
            if (phase != ChunkSize && ++headerBytes > HeaderLimit) return Fail("headers or trailers too large");
            if (cr)
            {
                if (c != '\n') return Fail("invalid line ending");
                cr = false;
                line[used] = 0;
                if (!Line()) return false;
                used = 0;
            }
            else if (c == '\r')
            {
                cr = true;
            }
            else
            {
                if (c == '\n' || ((uint8_t)c < 32 && c != '\t') || (uint8_t)c >= 127) return Fail("invalid header byte");
                if (used == sizeof(line) - 1) return Fail("response line too long");
                line[used++] = c;
            }
        }
        return true;
    }

    bool End()
    {
        if (error) return false;
        if (phase == Close)
        {
            phase = Complete;
            return true;
        }
        return phase == Complete ? true : Fail("truncated response");
    }

  private:
    enum Phase { StatusLine, Fields, Fixed, Close, ChunkSize, ChunkData, ChunkCR, ChunkLF, Trailers, Complete };
    Phase phase = StatusLine;
    Body body;
    Headers headers;
    void* context;
    unsigned bodyLimit;
    const char* error = nullptr;
    unsigned status = 0, received = 0, remaining = 0, contentLength = 0;
    unsigned used = 0, headerBytes = 0, fieldCount = 0, interim = 0, wire = 0;
    bool plainText = false, hasType = false, hasLocation = false;
    bool cr = false, hasLength = false, chunked = false, encoded = false, http10 = false;
    char line[2049] = {}, location[1025] = {};

    bool Fail(const char* message)
    {
        error = message;
        return false;
    }

    bool Line()
    {
        if (phase == StatusLine)
        {
            if (used < 13 || line[0] != 'H' || line[1] != 'T' || line[2] != 'T' || line[3] != 'P' ||
                line[4] != '/' || line[5] != '1' || line[6] != '.' || (line[7] != '0' && line[7] != '1') ||
                line[8] != ' ' || line[9] < '1' || line[9] > '5' || line[10] < '0' || line[10] > '9' ||
                line[11] < '0' || line[11] > '9' || line[12] != ' ')
            {
                return Fail("unsupported or malformed HTTP status");
            }
            status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
            http10 = line[7] == '0';
            phase = Fields;
            return true;
        }
        if (phase == ChunkSize) return Chunk();
        if (!used)
        {
            if (phase == Trailers)
            {
                phase = Complete;
                return true;
            }
            if (hasLength && chunked) return Fail("ambiguous HTTP body framing");
            if (status < 200)
            {
                if (status == 101 || hasLength || chunked || ++interim > 4) return Fail("unsupported informational response");
                phase = StatusLine;
                encoded = false;
                location[0] = 0;
                plainText = hasType = hasLocation = false;
                return true;
            }
            if (encoded) return Fail("compressed responses are unsupported");
            if ((status == 204 && (hasLength || chunked)) || (chunked && http10)) return Fail("invalid body framing for status/version");
            bool empty = status == 204 || status == 304;
            if (!empty && hasLength && (contentLength > bodyLimit || (status == 205 && contentLength))) return Fail("response body too large");
            if (headers && !headers(context, status, location)) return Fail("output could not be opened");
            remaining = contentLength;
            phase = empty ? Complete : chunked ? ChunkSize : hasLength ? (remaining ? Fixed : Complete) : Close;
            return true;
        }
        if (++fieldCount > 64) return Fail("too many response fields");
        unsigned colon = 0;
        while (line[colon] && line[colon] != ':')
        {
            if (!Token(line[colon])) return Fail("invalid header name or folding");
            ++colon;
        }
        if (!colon || line[colon] != ':') return Fail("invalid header field");
        line[colon] = 0;
        char* value = line + colon + 1;
        while (*value == ' ' || *value == '\t') ++value;
        unsigned end = used;
        while (end > colon + 1 && (line[end - 1] == ' ' || line[end - 1] == '\t')) line[--end] = 0;
        bool length = Equal(line, "Content-Length"), transfer = Equal(line, "Transfer-Encoding");
        if (phase == Trailers)
        {
            if (length || transfer || Equal(line, "Content-Encoding") || Equal(line, "Host") || Equal(line, "Connection")) return Fail("forbidden trailer field");
            return true;
        }
        if (length)
        {
            if (hasLength || !*value) return Fail("duplicate or empty Content-Length");
            unsigned number = 0;
            for (const char* p = value; *p; ++p)
            {
                if (*p < '0' || *p > '9' || number > (0xffffffffu - (unsigned)(*p - '0')) / 10) return Fail("invalid Content-Length");
                number = number * 10 + *p - '0';
            }
            hasLength = true;
            contentLength = number;
        }
        else if (transfer)
        {
            if (chunked || !Equal(value, "chunked")) return Fail("unsupported or duplicate Transfer-Encoding");
            chunked = true;
        }
        else if (Equal(line, "Content-Encoding"))
        {
            if (!Equal(value, "identity")) encoded = true;
        }
        else if (Equal(line, "Content-Type"))
        {
            if (hasType)
            {
                return Fail("duplicate Content-Type");
            }
            hasType = true;
            unsigned i = 0;
            while (value[i] && value[i] != ';' && value[i] != ' ' && value[i] != '\t')
            {
                ++i;
            }
            value[i] = 0;
            plainText = Equal(value, "text/plain");
        }
        else if (Equal(line, "Location"))
        {
            if (hasLocation) return Fail("duplicate Location");
            hasLocation = true;
            unsigned i = 0;
            while (value[i])
            {
                if (i == sizeof(location) - 1) return Fail("redirect URL too long");
                location[i] = value[i];
                ++i;
            }
            location[i] = 0;
        }
        return true;
    }

    bool Chunk()
    {
        unsigned at = 0, number = 0;
        while (Hex(line[at]) >= 0)
        {
            unsigned digit = Hex(line[at++]);
            if (number > (0xffffffffu - digit) / 16) return Fail("chunk too large");
            number = number * 16 + digit;
        }
        if (!at || number > bodyLimit - received) return Fail("invalid chunk size");
        // Ignore syntactically valid bounded chunk extensions.
        while (line[at])
        {
            while (line[at] == ' ' || line[at] == '\t') ++at;
            if (line[at++] != ';') return Fail("invalid chunk extension");
            while (line[at] == ' ' || line[at] == '\t') ++at;
            unsigned start = at;
            while (Token(line[at])) ++at;
            if (at == start) return Fail("invalid chunk extension name");
            while (line[at] == ' ' || line[at] == '\t') ++at;
            if (line[at] == '=')
            {
                ++at;
                while (line[at] == ' ' || line[at] == '\t') ++at;
                if (line[at] == '"')
                {
                    ++at;
                    while (line[at] && line[at] != '"')
                    {
                        if (line[at] == '\\' && !line[++at]) return Fail("invalid quoted extension");
                        ++at;
                    }
                    if (line[at++] != '"') return Fail("invalid quoted extension");
                }
                else
                {
                    start = at;
                    while (Token(line[at])) ++at;
                    if (at == start) return Fail("empty chunk extension value");
                }
            }
        }
        remaining = number;
        phase = number ? ChunkData : Trailers;
        return true;
    }
};
}
