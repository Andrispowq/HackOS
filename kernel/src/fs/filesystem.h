#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "drivers/device/device.h"

#define MAX_OPEN_FILES 256

struct Partition
{
    uint8_t attributes;
    uint8_t cylinder_start;
    uint8_t head_start;
    uint8_t sector_start;
    uint8_t filesystem;
    uint8_t cylinder_end;
    uint8_t head_end;
    uint8_t sector_end;
    uint32_t LBA_start;
    uint32_t size;
};

class ActiveFile
{
public:
    ActiveFile() : seek_position(0) {}
    ~ActiveFile() {}

public:
    size_t seek_position;
};

class Filesystem
{
public:
    Filesystem(Device* device) : device(device) {}
    ~Filesystem() {}

    virtual ActiveFile* OpenFile(const char* path) {}
    virtual int CloseFile(ActiveFile* file) {}

    virtual int CreateFile(const char* path) {}
    virtual int ResizeFile(const char* path, size_t newSize) {}
    virtual int DeleteFile(const char* path) {}

    virtual int GetDirectoryEntryCount(const char* path, uint64_t* count) {}
    virtual int GetFile(const char* path, uint64_t index, ActiveFile* file) {}

    virtual int Read(ActiveFile* file, void* buffer, uint64_t size) {}
    virtual int Write(ActiveFile* file, void* buffer, uint64_t size) {}

private:
    Device* device;
    uint64_t openFiles[MAX_OPEN_FILES];
};

#endif