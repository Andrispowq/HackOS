#ifndef FAT32_H
#define FAT32_H

#include "fs/filesystem.h"
#include "fat32driver.h"

struct FAT32_ActiveFile : public ActiveFile
{
	DirEntry entry;
	const char* path;
};

class FAT32 : public Filesystem
{
public:
    FAT32(Device* device);
    ~FAT32();

    virtual ActiveFile* OpenFile(const char* path) override;
    virtual void CloseFile(ActiveFile* file) override;

    virtual ActiveFile* CreateFile(const char* path, const char* name, permissions_t permissions) override;
    virtual void ResizeFile(ActiveFile* file, size_t newSize) override;
    virtual void DeleteFile(ActiveFile* file) override;

    virtual uint64_t GetDirectoryEntryCount(const char* path) override;
    virtual ActiveFile* GetFile(const char* path, uint64_t index) override;

    virtual int Read(ActiveFile* file, void* buffer, uint64_t size) override;
    virtual int Write(ActiveFile* file, void* buffer, uint64_t size) override;

private:
	FAT32Driver* driver;
};

#endif