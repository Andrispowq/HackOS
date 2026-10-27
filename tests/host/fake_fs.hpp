#pragma once
#include "support.hpp"
#include "fs/filesystem.h"

class FakeFile : public ActiveFile
{
  public:
    uint64_t length = 64;

    uint64_t GetSize() const override
    {
        return length;
    }

    const char* GetName() const override
    {
        return "FILE";
    }

    const char* GetPath() const override
    {
        return "~/FILE";
    }

    uint32_t GetAttributes() const override
    {
        return ARCHIVE;
    }
};

class FakeFS : public Filesystem
{
  public:
    FakeFS() : Filesystem(nullptr, 0)
    {
    }

    FakeFile file;
    bool missing = false, closed = false, created = false;
    char path[256] = {}, name[128] = {};
    unsigned char contents[4096] = {};
    uint64_t transferred = 0;
    int io_result = 0;

    ActiveFile* OpenFile(const char* p) override
    {
        copy_text(path, p, sizeof path);
        return missing ? nullptr : &file;
    }

    int Lookup(const char* p, ActiveFile*& result) override
    {
        result = OpenFile(p);
        return result ? 0 : -2;
    }

    void CloseFile(ActiveFile*) override
    {
        closed = true;
    }

    ActiveFile* CreateFile(const char* p, const char* n, permissions_t) override
    {
        copy_text(path, p, sizeof path);
        copy_text(name, n, sizeof name);
        created = true;
        return &file;
    }

    int ResizeFile(ActiveFile*, size_t) override
    {
        return io_result;
    }

    int DeleteFile(ActiveFile*) override
    {
        return io_result;
    }

    uint64_t GetDirectoryEntryCount(const char*) override
    {
        return 0;
    }

    ActiveFile* GetFile(const char*, uint64_t) override
    {
        return nullptr;
    }

    int Read(ActiveFile*, void* buffer, uint64_t n) override
    {
        if (n > sizeof contents)
        {
            test_abort("fake filesystem read too large");
        }

        transferred = n;
        test_copy(buffer, contents, n);
        return io_result;
    }

    int Write(ActiveFile*, void* buffer, uint64_t n) override
    {
        if (n > sizeof contents)
        {
            test_abort("fake filesystem write too large");
        }

        transferred = n;
        test_copy(contents, buffer, n);
        return io_result;
    }

  private:
    static void copy_text(char* dst, const char* src, size_t max)
    {
        size_t i = 0;
        for (; i + 1 < max && src[i]; ++i)
        {
            dst[i] = src[i];
        }

        dst[i] = 0;
    }
};
