#include "support.hpp"
#include "hackos/user.h"

static char captured[4096];
static uint64_t capturedLength;
static unsigned writes;
static bool failOutput;

bool User::Write(uint64_t fd, const void* data, uint64_t length)
{
    ++writes;
    if (failOutput || fd != HACKOS_STDOUT || length > 1024 || length > sizeof(captured) - capturedLength)
    {
        return false;
    }
    test_copy(captured + capturedLength, data, length);
    capturedLength += length;
    return true;
}

TEST(buffered_output_groups_fragments_and_preserves_order)
{
    capturedLength = writes = 0;
    failOutput = false;
    User::BufferedOutput output;
    for (unsigned i = 0; i < 1500; ++i)
    {
        output.Print("x");
    }
    CHECK(writes == 1 && capturedLength == 1024);
    output.Number(~0ULL);
    output.Print("\n");
    CHECK(output.Flush() && writes == 2 && capturedLength == 1521);
    for (unsigned i = 0; i < 1500; ++i)
    {
        CHECK(captured[i] == 'x');
    }
    CHECK(test_equal(captured + 1500, "18446744073709551615\n", 21));
    CHECK(output.Flush() && writes == 2);
    return 0;
}

TEST(buffered_output_reports_failure_without_retrying_bytes)
{
    capturedLength = writes = 0;
    failOutput = true;
    User::BufferedOutput output;
    output.Print("failed");
    CHECK(!output.Flush() && writes == 1 && !capturedLength);
    output.Print("ignored");
    CHECK(!output.Flush() && writes == 1);
    failOutput = false;
    return 0;
}
