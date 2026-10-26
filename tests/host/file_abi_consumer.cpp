#include "support.hpp"
// A separate translation unit is essential: it uses the actual libc declaration.
#include "../../libs/libc/src/file/file.h"
extern "C" int consume_libc_file(void* opaque)
{
    auto file = (ActiveFile*)opaque;
    CHECK(file->GetSize() == 0x12345678);
    CHECK(test_name_equal(file->GetName(), "ABI.TXT"));
    CHECK(test_name_equal(file->GetPath(), "~/ABI.TXT"));
    CHECK(file->GetAttributes() == 33);
    CHECK(file->seek_position == 23 && file->fs_index == 7);
    return 0;
}
