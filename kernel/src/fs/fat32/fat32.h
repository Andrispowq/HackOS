#ifndef FAT32_H
#define FAT32_H

#include "fs/filesystem.h"
#include "fat32driver.h"

class FAT32 : public Filesystem
{
public:
    FAT32(Device* device);
    ~FAT32();

    virtual int OpenFile(const char* path, void* file) {}
    virtual int CloseFile(void* file) {}

    virtual int CreateFile(const char* path) {}
    virtual int ResizeFile(const char* path, size_t newSize) {}
    virtual int DeleteFile(const char* path) {}

    virtual int GetDirectoryEntryCount(const char* path, uint64_t* count) {}
    virtual int GetFile(const char* path, uint64_t index, void* file) {}

    virtual int Read(void* file, void* buffer, uint64_t size) {}
    virtual int Write(void* file, void* buffer, uint64_t size) {}

private:
    FAT32Driver* driver;
};

#endif