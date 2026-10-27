#include "support.hpp"
#include "hackos/runtime/heap.h"
#include "hackos/runtime/completion.h"
#include "hackos/runtime/arguments.h"

alignas(16) static unsigned char arena[32768];
static size_t committed;
static bool rejectGrowth;

static bool Commit(size_t size)
{
    if (rejectGrowth)
    {
        return false;
    }

    committed = size;
    return true;
}

TEST(user_heap_alignment_split_coalesce_and_exhaustion)
{
    UserHeap heap;
    committed = 0;
    rejectGrowth = false;
    heap.Initialise(arena, sizeof(arena), Commit);
    CHECK(!heap.Allocate(0) && !heap.Allocate(~(size_t)0));
    auto a = (unsigned char*)heap.Allocate(27);
    auto b = (unsigned char*)heap.Allocate(5000);
    CHECK(a && b && !((uint64_t)a & 15) && !((uint64_t)b & 15));
    a[26] = 41;
    b[4999] = 42;
    CHECK(committed == 8192 && a[26] == 41 && b[4999] == 42);
    heap.Free(a);
    heap.Free(b);
    CHECK(heap.Allocate(12000) == a);
    heap.Free(a);
    auto whole = heap.Allocate(sizeof(arena) - 32);
    CHECK(whole == a && committed == sizeof(arena));
    CHECK(!heap.Allocate(1));
    heap.Free((void*)1234);
    heap.Free((unsigned char*)whole + 16);
    CHECK(!heap.Allocate(1));
    heap.Free(whole);
    heap.Free(whole);
    CHECK(heap.Allocate(sizeof(arena) - 32) == whole);
    return 0;
}

TEST(user_heap_failed_growth_realloc_preserves_payload)
{
    UserHeap heap;
    committed = 0;
    rejectGrowth = false;
    heap.Initialise(arena, sizeof(arena), Commit);
    auto a = (unsigned char*)heap.Allocate(32);
    for (unsigned i = 0; i < 32; ++i)
    {
        a[i] = i;
    }

    rejectGrowth = true;
    CHECK(!heap.Reallocate(a, 9000));
    CHECK(committed == 4096);
    rejectGrowth = false;
    auto b = (unsigned char*)heap.Reallocate(a, 9000);
    CHECK(b && b != a);
    for (unsigned i = 0; i < 32; ++i)
    {
        CHECK(b[i] == i);
    }

    CHECK(heap.Reallocate(b, 12) == b);
    CHECK(!heap.Reallocate(b, 0));
    CHECK(!heap.Reallocate((void*)1234, 5));
    CHECK(heap.Allocate(32) == a);
    return 0;
}

TEST(completion_quotes_escapes_and_command_position)
{
    CompletionWord word;
    CHECK(CompletionPrefix("  cat '/tmp/two wo", word));
    CHECK(word.index == 1 && word.start == 6);
    CHECK(test_equal(word.command, "cat", 4));
    CHECK(test_equal(word.text, "/tmp/two wo", 11));
    CHECK(CompletionPrefix("cd /tmp/two\\ words/", word));
    CHECK(word.index == 1 && CompletionMatch(word.text, "/TMP/TWO WORDS/"));
    CHECK(CompletionPrefix("print a ", word) && word.index == 2 && !word.text[0]);
    CHECK(!CompletionPrefix("cat trailing\\", word));
    CHECK(CompletionMatch("HELLO.TXT", "hel") && !CompletionMatch("ab", "abc"));
    return 0;
}

TEST(completion_replacement_preserves_parser_meaning_and_bounds)
{
    char output[256];
    CHECK(CompletionReplace("cat 'two", 4, "two words'&\\x", ' ', output));
    char* args[4];
    bool background;
    CHECK(ParseArguments(output, args, 4, background) == 2 && !background);
    CHECK(test_equal(args[1], "two words'&\\x", 13));
    char huge[256];
    test_fill(huge, 'a', 255);
    huge[255] = 0;
    CHECK(CompletionReplace("", 0, huge, 0, output));
    CHECK(!CompletionReplace("", 0, huge, ' ', output));
    CHECK(!CompletionReplace("cat ", 4, huge, 0, output));
    return 0;
}
