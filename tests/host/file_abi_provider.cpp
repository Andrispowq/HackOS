#include "support.hpp"
#include "fs/filesystem.h"

static unsigned destroyed;

class ABIFile : public ActiveFile
{
  public:
    ~ABIFile() override
    {
        ++destroyed;
    }

    uint64_t GetSize() const override
    {
        return 0x12345678;
    }

    const char* GetName() const override
    {
        return "ABI.TXT";
    }

    const char* GetPath() const override
    {
        return "~/ABI.TXT";
    }

    uint32_t GetAttributes() const override
    {
        return READ_ONLY | ARCHIVE;
    }
};

extern "C" int consume_libc_file(void*);

TEST(active_file_kernel_libc_binary_interface)
{
    destroyed = 0;
    ActiveFile* file = new ABIFile;
    file->seek_position = 23;
    file->fs_index = 7;
    int result = consume_libc_file(file);
    CHECK(destroyed == 0);
    delete file;
    CHECK(destroyed == 1);
    CHECK(result == 0);
    return 0;
}
