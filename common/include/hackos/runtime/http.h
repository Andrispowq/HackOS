#pragma once
#include "hackos/types.h"
#include "hackos/runtime/ipv4.h"
#include "hackos/runtime/hostname.h"

namespace Hackos::Http
{
constexpr unsigned BodyLimit = 32768, HeaderLimit = 8192, WireLimit = 262144;
inline char Lower(char c)
{
    return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
}
inline bool Equal(const char* a, const char* b)
{
    while (*a && *b)
    {
        if (Lower(*a++) != Lower(*b++)) return false;
    }
    return *a == *b;
}
inline int Hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = Lower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}
inline bool Token(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
    const char* punctuation = "!#$%&'*+-.^_`|~";
    for (unsigned i = 0; punctuation[i]; ++i)
    {
        if (c == punctuation[i]) return true;
    }
    return false;
}

struct Url
{
    char host[256], target[1025];
    uint16_t port;
};

inline bool ParseUrl(const char* text, Url& url)
{
    if (!text) return false;
    unsigned length = 0;
    while (length <= 1024 && text[length]) ++length;
    if (length < 8 || length > 1024) return false;
    const char* scheme = "http://";
    for (unsigned i = 0; i < 7; ++i)
    {
        if (Lower(text[i]) != scheme[i]) return false;
    }
    for (unsigned i = 7; i < length; ++i)
    {
        if ((uint8_t)text[i] <= 32 || (uint8_t)text[i] >= 127 || text[i] == '\\') return false;
        if (text[i] == '%' && (i + 2 >= length || Hex(text[i + 1]) < 0 || Hex(text[i + 2]) < 0)) return false;
    }
    unsigned at = 7, hostSize = 0;
    char host[256] = {};
    while (at < length && text[at] != ':' && text[at] != '/' && text[at] != '?' && text[at] != '#')
    {
        if (hostSize >= 253) return false;
        host[hostSize++] = text[at++];
    }
    if (!hostSize) return false;
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
    url.port = 80;
    if (at < length && text[at] == ':')
    {
        ++at;
        unsigned port = 0, digits = 0;
        while (at < length && text[at] >= '0' && text[at] <= '9')
        {
            port = port * 10 + text[at++] - '0';
            if (++digits > 5 || port > 65535) return false;
        }
        if (!digits || !port) return false;
        url.port = port;
    }
    if (at < length && text[at] != '/' && text[at] != '?' && text[at] != '#') return false;
    unsigned used = 0;
    if (at == length || text[at] != '/') url.target[used++] = '/';
    while (at < length && text[at] != '#')
    {
        url.target[used++] = text[at++];
    }
    url.target[used] = 0;
    return true;
}

// Streaming HTTP/1.x response decoder. Callbacks receive only final headers and
// decoded body bytes. No allocation; arbitrary TCP segmentation is supported.
class Decoder
{
  public:
    using Body = bool (*)(void*, const uint8_t*, unsigned);
    using Headers = bool (*)(void*, unsigned, const char*);
    Decoder(Body body, Headers headers, void* context) : body(body), headers(headers), context(context) {}
    unsigned Status() const { return status; }
    unsigned Received() const { return received; }
    bool Done() const { return phase == Complete; }
    const char* Error() const { return error; }

    bool Feed(const uint8_t* bytes, unsigned size)
    {
        if (error || (size && !bytes)) return false;
        if (size > WireLimit - wire) return Fail("response wire limit exceeded");
        wire += size;
        unsigned at = 0;
        while (at < size)
        {
            if (phase == Complete) return Fail("bytes after response end");
            if (phase == Fixed || phase == Close || phase == ChunkData)
            {
                unsigned count = size - at;
                if (phase != Close && count > remaining) count = remaining;
                if (count > BodyLimit - received || (status == 205 && count)) return Fail("response body limit or status violated");
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
    const char* error = nullptr;
    unsigned status = 0, received = 0, remaining = 0, contentLength = 0;
    unsigned used = 0, headerBytes = 0, fieldCount = 0, interim = 0, wire = 0;
    bool cr = false, hasLength = false, chunked = false, encoded = false, http10 = false;
    char line[1025] = {}, location[1025] = {};

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
                return true;
            }
            if (encoded) return Fail("compressed responses are unsupported");
            if ((status == 204 && (hasLength || chunked)) || (chunked && http10)) return Fail("invalid body framing for status/version");
            bool empty = status == 204 || status == 304;
            if (!empty && hasLength && (contentLength > BodyLimit || (status == 205 && contentLength))) return Fail("response body too large");
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
        else if (Equal(line, "Location"))
        {
            if (location[0]) return Fail("duplicate Location");
            unsigned i = 0;
            while (value[i])
            {
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
            if (number > (BodyLimit - digit) / 16) return Fail("chunk too large");
            number = number * 16 + digit;
        }
        if (!at || number > BodyLimit - received) return Fail("invalid chunk size");
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
