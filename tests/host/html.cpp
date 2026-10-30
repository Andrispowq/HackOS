#include "hackos/runtime/html.h"
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
    Url("../guide.html", "http://demo.test:8080/a/guide.html");
    Url("./", "http://demo.test:8080/a/b/");
    Url("/x/./y/../z", "http://demo.test:8080/x/z");
    Url("../../../z", "http://demo.test:8080/z");
    Url("?x=1&y=2", "http://demo.test:8080/a/b/page?x=1&y=2");
    Url("#anchor", "http://demo.test:8080/a/b/page?old=1");
    Url("", "http://demo.test:8080/a/b/page?old=1");
    Url("//other.test/path", "http://other.test/path");
    Url("http://other.test", "http://other.test/");
    Url("HTTPS://other.test", nullptr);
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
