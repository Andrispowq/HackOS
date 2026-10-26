#include "support.hpp"
#include "hackos/runtime/history.h"
#include "hackos/runtime/line_buffer.h"
TEST(history_recall_draft_and_immutable_entries)
{
    static CommandHistory h;
    CHECK(test_name_equal(h.Up("draft"),"draft"));
    h.Add("first"); h.Add("second");
    CHECK(test_name_equal(h.Up("pending"),"second"));
    CHECK(test_name_equal(h.Up("edited"),"first"));
    CHECK(test_name_equal(h.Up("edited"),"first"));
    CHECK(test_name_equal(h.Down(""),"second"));
    CHECK(test_name_equal(h.Down(""),"pending"));
    CHECK(test_name_equal(h.Down("new"),"new"));
    CHECK(test_name_equal(h.At(1),"second"));
    h.Add(" "); h.Add(""); CHECK(h.Count() == 2);
    h.Add("second"); CHECK(h.Count() == 3); // executed duplicates are retained
    h.ResetNavigation(); CHECK(test_name_equal(h.Down(""),""));
    return 0;
}
TEST(history_ring_capacity_and_long_commands)
{
    static CommandHistory h;
    char text[256];
    for (unsigned i=0;i<300;++i)
    {
        for (unsigned j=0;j<255;++j) text[j] = 'a' + i%26;
        text[255] = 0; h.Add(text);
    }
    CHECK(h.Count() == 256);
    CHECK(h.At(0)[0] == 'a' + 44%26 && h.At(255)[0] == 'a' + 299%26);
    CHECK(h.At(255)[254] && !h.At(255)[255] && !h.At(256)[0]);
    LineBuffer line; line.Set(h.At(255)); CHECK(line.Length() == 255);
    CHECK(line.Push('\b') == LineBuffer::Erase && line.Length() == 254);
    CHECK(h.At(255)[254]); // editing a recalled copy never edits stored history
    for (int i=0;i<300;++i) h.Up("");
    CHECK(h.Up("")[0] == 'a'+44%26);
    return 0;
}
