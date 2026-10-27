#include "support.hpp"
#include "hackos/runtime/path.h"

TEST(paths_root_relative_and_parent)
{
    char path[256];
    CHECK(NormalizePath("../bin/./ps", "~/USR/LOCAL", path));
    CHECK(test_name_equal(path, "~/USR/BIN/PS"));
    CHECK(NormalizePath("/usr//bin/ps", "~/OTHER", path));
    CHECK(test_name_equal(path, "~/USR/BIN/PS"));
    CHECK(NormalizePath("../../../", "~/USR", path));
    CHECK(test_name_equal(path, "~"));
    CHECK(NormalizePath("~/usr/hello.txt", "~", path));
    CHECK(test_name_equal(path, "~/USR/HELLO.TXT"));
    return 0;
}

TEST(paths_reject_empty_invalid_and_overflow)
{
    char path[256];
    CHECK(!NormalizePath("", "~", path));
    CHECK(!NormalizePath("~someone/file", "~", path));
    CHECK(!NormalizePath("a\\b", "~", path));
    char longName[256];
    for (unsigned i = 0; i < 255; i++)
    {
        longName[i] = 'x';
    }

    longName[255] = 0;
    CHECK(!NormalizePath(longName, "~", path));
    longName[253] = 0;
    CHECK(NormalizePath(longName, "~", path));
    CHECK(path[255] == 0);
    return 0;
}
