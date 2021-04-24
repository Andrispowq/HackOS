#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "drivers/device/device.h"
#include "openfile.h"

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

class Filesystem
{
public:
    Filesystem(Device* device) : device(device) {}
    ~Filesystem() {}

    virtual int OpenFile(const char* path, OpenFile* file) = 0;
    virtual int CloseFile(void* file) = 0;

    virtual int CreateFile(const char* path) = 0;
    virtual int ResizeFile(const char* path, size_t newSize) = 0;
    virtual int DeleteFile(const char* path) = 0;

    virtual int GetDirectoryEntryCount(const char* path, uint64_t* count) = 0;
    virtual int GetFile(const char* path, uint64_t index, void* file) = 0;

    virtual int Read(void* file, void* buffer, uint64_t size) = 0;
    virtual int Write(void* file, void* buffer, uint64_t size) = 0;

private:
    Device* device;
    uint64_t openFiles[MAX_OPEN_FILES];
};

#endif