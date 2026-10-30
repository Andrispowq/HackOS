#include "hackos/runtime/http.h"
#include <cassert>

using namespace Hackos::Http;
struct Sink
{
    uint8_t data[BodyLimit + 1] = {};
    unsigned size = 0, status = 0, headers = 0;
    bool reject = false;
    static bool Body(void* context, const uint8_t* data, unsigned size)
    {
        auto& sink = *(Sink*)context;
        if (sink.reject) return false;
        assert(size <= BodyLimit - sink.size);
        for (unsigned i = 0; i < size; ++i)
        {
            sink.data[sink.size++] = data[i];
        }
        return true;
    }
    static bool Headers(void* context, unsigned status, const char*)
    {
        auto& sink = *(Sink*)context;
        sink.status = status;
        ++sink.headers;
        return true;
    }
};

static void Good(const char* message, const char* body, unsigned status = 200)
{
    unsigned length = __builtin_strlen(message), expected = __builtin_strlen(body);
    for (unsigned split = 0; split <= length; ++split)
    {
        Sink sink;
        Decoder d(Sink::Body, Sink::Headers, &sink);
        assert(d.Feed((const uint8_t*)message, split));
        assert(d.Feed((const uint8_t*)message + split, length - split));
        assert(d.End() && d.Done());
        assert(sink.headers == 1 && sink.status == status && sink.size == expected && d.Received() == expected);
        assert(!__builtin_memcmp(sink.data, body, expected));
    }
    Sink sink;
    Decoder d(Sink::Body, Sink::Headers, &sink);
    for (unsigned i = 0; i < length; ++i)
    {
        assert(d.Feed((const uint8_t*)message + i, 1));
    }
    assert(d.End() && sink.size == expected);
}

static void Bad(const char* message)
{
    unsigned size = __builtin_strlen(message);
    Sink sink;
    Decoder d(Sink::Body, Sink::Headers, &sink);
    bool valid = true;
    for (unsigned i = 0; i < size && valid; ++i)
    {
        valid = d.Feed((const uint8_t*)message + i, 1);
    }
    assert(!valid || !d.End());
    assert(d.Error());
}

int main()
{
    Url url;
    assert(ParseUrl("http://Example.COM:8080/a%20b?q=one#ignored", url));
    assert(Equal(url.host, "example.com") && url.port == 8080 && Equal(url.target, "/a%20b?q=one"));
    assert(ParseUrl("HTTP://10.0.2.2?x=1", url) && Equal(url.target, "/?x=1") && url.port == 80);
    assert(ParseUrl("http://example.com#fragment", url) && Equal(url.target, "/"));
    const char* invalid[] = {"", "http://", "https://example.com/", "http://user@example.com/", "http://[::1]/",
        "http://example.com:0/", "http://example.com:65536/", "http://example.com:/", "http://bad..host/",
        "http://1.2.3.999/", "http://example.com/hello world", "http://example.com/\r\nInjected:yes",
        "http://example.com/%0", "http://example.com/%zz", "http://example.com\\evil/"};
    for (const char* text : invalid)
    {
        assert(!ParseUrl(text, url));
    }
    Good("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", "hello");
    Good("HTTP/1.0 200 OK\r\n\r\nhello", "hello");
    Good("HTTP/1.1 200 \r\ncontent-length:\t 0 \t\r\n\r\n", "");
    Good("HTTP/1.1 103 Early Hints\r\nLink: </x>\r\n\r\nHTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", "ok");
    Good("HTTP/1.1 200 OK\r\nTransfer-Encoding: ChUnKeD\r\n\r\n2;foo=bar; quoted=\"a\\\"b\"\r\nhi\r\n3\r\nbye\r\n0\r\nX-Test: yes\r\n\r\n", "hibye");
    Good("HTTP/1.1 204 No Content\r\n\r\n", "", 204);
    Good("HTTP/1.1 304 Not Modified\r\nContent-Length: 99999\r\n\r\n", "", 304);
    Good("HTTP/1.1 404 Missing\r\nContent-Length: 4\r\n\r\noops", "oops", 404);
    const char* bad[] = {
        "", "HTTP/1.1 200 OK\n\n", "HTTP/2.0 200 OK\r\n\r\n", "HTTP/1.1 999 OK\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nxx",
        "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 42949672960\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 32769\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 1, 1\r\n\r\nx",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n",
        "HTTP/1.1 200 OK\r\n Content-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length : 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nX: a\rbroken\r\n\r\n",
        "HTTP/1.1 101 Switching Protocols\r\n\r\n",
        "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 205 Reset\r\nContent-Length: 1\r\n\r\nx",
        "HTTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n8001\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n+1\r\nx\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;bad=\"\r\nx\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;bad=\r\nx\r\n0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\nx\n0\r\n\r\n"
    };
    for (const char* text : bad)
    {
        Bad(text);
    }
    // Exact parser boundaries, binary body bytes, sink failure, and full-size body.
    Sink sink;
    Decoder d(Sink::Body, Sink::Headers, &sink);
    const char* header = "HTTP/1.1 200 OK\r\nContent-Length: 32768\r\n\r\n";
    assert(d.Feed((const uint8_t*)header, __builtin_strlen(header)));
    uint8_t block[512];
    for (unsigned i = 0; i < 512; ++i)
    {
        block[i] = i;
    }
    for (unsigned i = 0; i < 64; ++i)
    {
        assert(d.Feed(block, sizeof(block)));
    }
    assert(d.Done() && d.End() && sink.size == BodyLimit);
    assert(sink.data[0] == 0 && sink.data[255] == 255);
    Sink rejected;
    rejected.reject = true;
    Decoder failure(Sink::Body, Sink::Headers, &rejected);
    const char* msg = "HTTP/1.1 200 OK\r\n\r\nx";
    assert(!failure.Feed((const uint8_t*)msg, __builtin_strlen(msg)) && failure.Error());
    char longLine[1100];
    const char* prefix = "HTTP/1.1 200 OK\r\nX: ";
    unsigned pos = __builtin_strlen(prefix);
    __builtin_memcpy(longLine, prefix, pos);
    while (pos < sizeof(longLine) - 1) longLine[pos++] = 'x';
    longLine[pos] = 0;
    Bad(longLine);
    // EOF at every position in a fixed-length or chunked response must fail until complete.
    const char* complete[] = {"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok",
                              "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nok\r\n0\r\n\r\n"};
    for (const char* text : complete)
    {
        for (unsigned n = 0; n < __builtin_strlen(text); ++n)
        {
            auto* exact = new uint8_t[n];
            __builtin_memcpy(exact, text, n);
            Sink truncated;
            Decoder parser(Sink::Body, Sink::Headers, &truncated);
            assert(parser.Feed(exact, n) && !parser.End());
            delete[] exact;
        }
    }
    char maximumUrl[1026];
    const char* urlPrefix = "http://x.test/";
    unsigned prefixLength = __builtin_strlen(urlPrefix);
    __builtin_memcpy(maximumUrl, urlPrefix, prefixLength);
    for (unsigned i = prefixLength; i < 1025; ++i)
    {
        maximumUrl[i] = 'a';
    }
    maximumUrl[1024] = 0;
    assert(ParseUrl(maximumUrl, url));
    maximumUrl[1024] = 'a';
    maximumUrl[1025] = 0;
    assert(!ParseUrl(maximumUrl, url));
    Sink limits;
    Decoder fields(Sink::Body, Sink::Headers, &limits);
    const char* statusLine = "HTTP/1.1 200 OK\r\n";
    assert(fields.Feed((const uint8_t*)statusLine, __builtin_strlen(statusLine)));
    const char* field = "X: y\r\n";
    for (unsigned i = 0; i < 64; ++i)
    {
        assert(fields.Feed((const uint8_t*)field, __builtin_strlen(field)));
    }
    assert(!fields.Feed((const uint8_t*)field, __builtin_strlen(field)));
    Decoder headerLimit(Sink::Body, Sink::Headers, &limits);
    assert(headerLimit.Feed((const uint8_t*)statusLine, __builtin_strlen(statusLine)));
    char wideField[1005];
    wideField[0] = 'X';
    wideField[1] = ':';
    for (unsigned i = 2; i < 1000; ++i)
    {
        wideField[i] = 'a';
    }
    wideField[1000] = '\r';
    wideField[1001] = '\n';
    for (unsigned i = 0; i < 8; ++i)
    {
        assert(headerLimit.Feed((const uint8_t*)wideField, 1002));
    }
    assert(!headerLimit.Feed((const uint8_t*)wideField, 1002));
    Decoder metadata(Sink::Body, Sink::Headers, &limits);
    const char* chunkHeader = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    assert(metadata.Feed((const uint8_t*)chunkHeader, __builtin_strlen(chunkHeader)));
    wideField[0] = '1';
    wideField[1] = ';';
    wideField[1002] = 'x';
    wideField[1003] = '\r';
    wideField[1004] = '\n';
    unsigned chunks = 0;
    while (metadata.Feed((const uint8_t*)wideField, sizeof(wideField))) ++chunks;
    assert(chunks > 200 && chunks < 300 && metadata.Error());
    // Mutated valid responses must remain memory safe even if their syntax survives.
    const char* corpus = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2;x=\"y\"\r\nok\r\n0\r\n\r\n";
    unsigned corpusSize = __builtin_strlen(corpus);
    for (unsigned at = 0; at < corpusSize; ++at)
    {
        for (unsigned value = 0; value < 128; value += 7)
        {
            auto* bytes = new uint8_t[corpusSize];
            __builtin_memcpy(bytes, corpus, corpusSize);
            bytes[at] = value;
            Sink mutated;
            Decoder parser(Sink::Body, Sink::Headers, &mutated);
            if (parser.Feed(bytes, corpusSize)) parser.End();
            delete[] bytes;
        }
    }

}
