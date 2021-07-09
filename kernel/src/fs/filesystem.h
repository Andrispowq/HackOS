#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "drivers/device/device.h"

typedef uint32_t permissions_t;

#define READ_ONLY (0x1 << 0)
#define HIDDEN (0x1 << 1)
#define SYSTEM (0x1 << 2)
#define VOLUME_ID (0x1 << 3)
#define DIRECTORY (0x1 << 4)
#define ARCHIVE (0x1 << 5)

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

    virtual uint64_t GetSize() const = 0;
    virtual const char* GetName() const = 0;

public:
    size_t seek_position;
};

class Filesystem
{
public:
    Filesystem(Device* device) : device(device) {}
    ~Filesystem() {}

    virtual ActiveFile* OpenFile(const char* path) = 0;
    virtual void CloseFile(ActiveFile* file) = 0;

    virtual ActiveFile* CreateFile(const char* path, const char* name, permissions_t permissions) = 0;
    virtual void ResizeFile(ActiveFile* file, size_t newSize) = 0;
    virtual void DeleteFile(ActiveFile* file) = 0;

    virtual uint64_t GetDirectoryEntryCount(const char* path) = 0;
    virtual ActiveFile* GetFile(const char* path, uint64_t index) = 0;

    virtual int Read(ActiveFile* file, void* buffer, uint64_t size) = 0;
    virtual int Write(ActiveFile* file, void* buffer, uint64_t size) = 0;

private:
    Device* device;
};

#endif