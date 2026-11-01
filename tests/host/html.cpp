#include "hackos/runtime/html.h"
#include "hackos/runtime/html_preview.h"
#include <cassert>
using namespace Hackos::Html;
static Document document;

static void Load(const char* text)
{
    document.Load(text, __builtin_strlen(text));
}

static void Text(const char* expected)
{
    assert(document.count == __builtin_strlen(expected));
    for (unsigned i = 0; i < document.count; ++i)
    {
        assert(document.glyphs[i].text == expected[i]);
    }
}

static void Url(const char* reference, const char* expected)
{
    char out[256];
    bool good = Resolve("http://demo.test:8080/a/b/page?old=1", reference, out);
    assert(good == (expected != nullptr));
    if (good)
    {
        assert(!__builtin_strcmp(out, expected));
    }
}

int main()
{
    const char* page = "<head><script>const x='</head>';</script></head><p class='noise'>Hello</p>"
                       "<script>if (a < b) { garbage; }</script><style>ignored</style><!-- hidden -->"
                       "<span id='target'>world</span><a href='/next'>Next</a>";
    for (unsigned split = 0; split <= __builtin_strlen(page); ++split)
    {
        Preview preview;
        char output[32768];
        unsigned size = 0;
        preview.Feed((const uint8_t*)page, split, output, sizeof(output), size);
        preview.Feed((const uint8_t*)page + split, __builtin_strlen(page) - split, output, sizeof(output), size);
        document.Load(output, size);
        assert(!preview.limited && document.linkCount == 1 && document.anchorCount == 1);
        Text("Hello\n\nworldNext");
    }
    {
        Preview preview;
        char output[32768];
        unsigned size = 0;
        const char* start = "<script>";
        preview.Feed((const uint8_t*)start, 8, output, sizeof(output), size);
        const uint8_t byte = 'x';
        for (unsigned n = 0; n < 900000; ++n)
        {
            preview.Feed(&byte, 1, output, sizeof(output), size);
        }
        const char* end = "</script><p>After huge script</p>";
        for (unsigned n = 0; n < __builtin_strlen(end); ++n)
        {
            preview.Feed((const uint8_t*)end + n, 1, output, sizeof(output), size);
        }
        document.Load(output, size);
        Text("After huge script\n\n");
        assert(!preview.limited);
        for (unsigned n = 0; n < 40000; ++n)
        {
            preview.Feed(&byte, 1, output, sizeof(output), size, true);
        }
        assert(preview.limited && size == sizeof(output));
    }

    Url("../guide.html", "http://demo.test:8080/a/guide.html");
    Url("./", "http://demo.test:8080/a/b/");
    Url("/x/./y/../z", "http://demo.test:8080/x/z");
    Url("../../../z", "http://demo.test:8080/z");
    Url("?x=1&y=2", "http://demo.test:8080/a/b/page?x=1&y=2");
    Url("#anchor", "http://demo.test:8080/a/b/page?old=1#anchor");
    Url("", "http://demo.test:8080/a/b/page?old=1");
    Url("//other.test/path", "http://other.test/path");
    Url("http://other.test", "http://other.test/");
    Url("HTTPS://other.test", "HTTPS://other.test/");
    char secure[256];
    assert(Resolve("https://demo.test/a", "//other.test/b", secure));
    assert(!__builtin_strcmp(secure, "https://other.test/b"));
    Url("javascript:alert(1)", nullptr);
    Url("file:///x", nullptr);
    Url("http://user@host/", nullptr);
    Url("/x\r\nHost:evil", nullptr);
    Url("/%bad%", nullptr);
    Load("one  two\nthree &amp; &lt; &gt; &quot; &apos; &#65; &#x42; &#9999;");
    Text("one two three & < > \" ' A B ?");
    Load("<head><title>hidden</title><style>x</style></head><p>visible</p><script>x</script><style>x</style>");
    Text("visible\n\n");
    Load("<b>bold <strong>nested</strong> bold</b> plain");
    assert(document.glyphs[0].style & Bold);
    assert(document.glyphs[12].style & Bold);
    assert(!(document.glyphs[document.count - 1].style & Bold));
    Load("<pre>a\n  b\tc</pre>");
    Text("a\n  b    c\n\n");
    Load("<h1>Title</h1><ul><li>first</li><li>second</li></ul>");
    assert(document.glyphs[0].style & Heading);
    Load("<a title='a > b' href='one?x=1&amp;y=2'>One</a> <A HREF=../two>Two</A>");
    Text("One Two");
    assert(document.linkCount == 2 && document.glyphs[0].link == 1 && document.glyphs[4].link == 2);
    assert(!__builtin_strcmp(document.links[0], "one?x=1&y=2"));
    Load("<unknown>visible</unknown><!--hidden--> text");
    Text("visible text");
    Load("hello world longword");
    document.Layout(8);
    assert(document.rows[6] == 1 && document.columns[6] == 0);
    document.Layout(1);
    for (unsigned i = 0; i < document.count; ++i)
    {
        assert(document.columns[i] == 0);
    }
    Load("<p id='Top'>first</p><div id='a&amp;b'>second</div><a name=legacy>third</a><p id=Top>duplicate</p>");
    document.Layout(80);
    assert(document.anchorCount == 4);
    assert(document.Anchor("http://x/#Top") == 0);
    assert(document.Anchor("http://x/#a%26b") > 0);
    assert(document.Anchor("http://x/#legacy") > document.Anchor("http://x/#a%26b"));
    assert(document.Anchor("http://x/#missing") == 0);
    document.Layout(1);
    assert(document.Anchor("http://x/#legacy") > document.Anchor("http://x/#a%26b"));
    const char* plain = "<b>literal</b> &amp;\r\n  tabs\t!";
    document.Load(plain, __builtin_strlen(plain), true);
    Text("<b>literal</b> &amp;\n  tabs    !");
    assert(!document.linkCount && !document.anchorCount);
    // Exact-sized buffers exercise every truncation and mutated tokenizer state.
    const char* fixture = "<p>a&amp;b <a href=\"../x?y=1&amp;z=2\">link</a></p><pre>a\nb</pre><script>hidden</script>";
    unsigned size = __builtin_strlen(fixture);
    for (unsigned n = 0; n <= size; ++n)
    {
        char* buffer = new char[n ? n : 1];
        __builtin_memcpy(buffer, fixture, n);
        document.Load(buffer, n);
        document.Layout(1);
        delete[] buffer;
    }
    for (unsigned i = 0; i < size; ++i)
    {
        for (unsigned byte = 0; byte < 256; byte += 7)
        {
            char buffer[128];
            __builtin_memcpy(buffer, fixture, size);
            buffer[i] = byte;
            document.Load(buffer, size);
            document.Layout(17);
            assert(document.count <= Limit && document.linkCount <= LinkLimit);
            Preview preview;
            char compact[256];
            unsigned retained = 0;
            for (unsigned at = 0; at < size; ++at)
            {
                preview.Feed((const uint8_t*)buffer + at, 1, compact, sizeof(compact), retained);
            }
            document.Load(compact, retained);
            document.Layout(17);
            assert(document.count <= Limit && document.linkCount <= LinkLimit);
        }
    }
    char* huge = new char[Limit];
    for (unsigned i = 0; i < Limit; ++i)
    {
        huge[i] = 'x';
    }
    document.Load(huge, Limit);
    document.Layout(1);
    assert(document.count == Limit && document.lineCount == Limit);
    for (unsigned i = 0; i < Limit; ++i)
    {
        huge[i] = '\n';
    }
    __builtin_memcpy(huge, "<pre>", 5);
    document.Load(huge, Limit);
    document.Layout(1);
    assert(document.first[document.lineCount] == document.count);
    for (unsigned i = 5; i < Limit; ++i)
    {
        huge[i] = '\t';
    }
    document.Load(huge, Limit);
    assert(document.count == Limit && document.limited);
    delete[] huge;
    char links[4096] = {};
    unsigned used = 0;
    const char* item = "<a href='/next'>x</a>";
    for (unsigned i = 0; i < 70; ++i)
    {
        __builtin_memcpy(links + used, item, __builtin_strlen(item));
        used += __builtin_strlen(item);
    }
    document.Load(links, used);
    assert(document.linkCount == LinkLimit && document.count == 70);
    assert(document.glyphs[63].link == 64 && !document.glyphs[64].link);
    char oversized[258];
    for (unsigned i = 0; i < 256; ++i)
    {
        oversized[i] = 'a';
    }
    oversized[256] = 0;
    char resolved[256];
    assert(!Resolve("http://host/", oversized, resolved));
}
