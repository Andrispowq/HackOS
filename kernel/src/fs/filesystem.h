#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "drivers/device/device.h"
#include "permissions.h"

#define MAX_FILESYSTEMS MAX_DEVICE_COUNT * 4

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
    ActiveFile() : seek_position(0)
    {
    }

    virtual ~ActiveFile()
    {
    }

    virtual uint64_t GetSize() const = 0;
    virtual const char* GetName() const = 0;
    virtual const char* GetPath() const = 0;

    virtual uint32_t GetAttributes() const = 0;

  public:
    virtual FilePermissions GetPermissions() const
    {
        return {0, 0, 0};
    }

    size_t seek_position = 0;
    size_t fs_index = -1;
    uint32_t accessMode = 0;
};

class Filesystem
{
  public:
    Filesystem(Device* device, uint32_t part_start) : device(device), PartitionStart(part_start)
    {
    }

    virtual ~Filesystem()
    {
    }

    virtual ActiveFile* OpenFile(const char* path) = 0;
    virtual void CloseFile(ActiveFile* file) = 0;

    // Checked operations used by the isolated user ABI. -2 means missing;
    // all other nonzero results are errors. Unsupported filesystems fail closed.
    virtual int Lookup(const char* path, ActiveFile*& file)
    {
        file = OpenFile(path);
        return file ? 0 : -1;
    }

    virtual int MakeDirectory(const char* path, const FilePermissions* permissions = nullptr)
    {
        return -1;
    }

    virtual int RemoveFile(const char* path)
    {
        return -1;
    }

    virtual int MoveFile(const char* source, const char* destination)
    {
        return -1;
    }

    virtual int SaveFile(const char* path, const void* data, uint32_t size, bool create,
                         const FilePermissions* permissions = nullptr)
    {
        return -1;
    }

    virtual ActiveFile* CreateFile(const char* path, const char* name, permissions_t permissions) = 0;
    virtual int ResizeFile(ActiveFile* file, size_t newSize) = 0;
    virtual int DeleteFile(ActiveFile* file) = 0;

    virtual uint64_t GetDirectoryEntryCount(const char* path) = 0;
    virtual ActiveFile* GetFile(const char* path, uint64_t index) = 0;

    // Kernel-only access to fixed, preallocated account-store sectors.
    // Implementations must reject malformed, overlapping or short chains.
    virtual bool AccountStorage(uint64_t (&sectors)[2][16], Device*& storage)
    {
        return false;
    }

    virtual int SetPermissions(const char* path, const FilePermissions& permissions)
    {
        return -1;
    }

    virtual int Flush()
    {
        return -1;
    }

    // Copied metadata only: no persistent handles or cross-call cache.
    virtual int ReadDirectory(const char* path, uint64_t index, HackosFileInfo* output, uint64_t capacity,
                              uint64_t& count)
    {
        count = 0;
        return -1;
    }

    virtual int DirectoryCount(const char* path, uint64_t& count)
    {
        return -1;
    }

    virtual int Read(ActiveFile* file, void* buffer, uint64_t size) = 0;
    virtual int Write(ActiveFile* file, void* buffer, uint64_t size) = 0;

  private:
    Device* device;
    uint32_t PartitionStart;
};

extern Filesystem* filesystems[MAX_FILESYSTEMS];

void InitialiseFilesystems();
void RegisterFilesystem(Filesystem* filesystem);
void UnregisterFilesystem(Filesystem* filesystem); //Doesn't delete the device

#endif
