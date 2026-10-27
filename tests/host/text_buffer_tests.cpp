#include "support.hpp"
#include "hackos/runtime/text_buffer.h"

TEST(text_buffer_insert_delete_navigation)
{
    TextBuffer text;
    for (char ch : "one\ntwo\n")
    {
        if (ch)
        {
            CHECK(text.Key(ch));
        }
    }

    CHECK(text.length == 8 && text.cursor == 8 && text.Valid());
    text.Key(HACKOS_KEY_UP);
    CHECK(text.cursor == 4);
    text.Key(HACKOS_KEY_RIGHT);
    text.Key(HACKOS_KEY_UP);
    CHECK(text.cursor == 1);
    text.Key(HACKOS_KEY_DELETE);
    text.Key('N');
    CHECK(test_equal(text.text, "oNe\ntwo\n", 8));
    text.Key('\b');
    text.Key('\b');
    CHECK(test_equal(text.text, "e\ntwo\n", 6));
    text.Key(HACKOS_KEY_LEFT);
    text.Key('\b');
    CHECK(text.cursor == 0 && text.length == 6);
    text.cursor = text.length;
    text.Key(HACKOS_KEY_DELETE);
    CHECK(text.length == 6);
    return 0;
}

TEST(text_buffer_limit_and_binary_rejection)
{
    TextBuffer text;
    for (unsigned i = 0; i < HACKOS_FILE_LIMIT; ++i)
    {
        CHECK(text.Key('x'));
    }

    CHECK(!text.Key('y') && text.length == HACKOS_FILE_LIMIT);
    text.Key('\b');
    CHECK(text.Key('\n') && text.Valid());
    text.text[8] = 0;
    CHECK(!text.Valid());
    text.text[8] = '\t';
    CHECK(text.Valid());
    text.length = HACKOS_FILE_LIMIT + 1;
    CHECK(!text.Valid());
    return 0;
}
