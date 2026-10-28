#ifndef FAT32_H
#define FAT32_H

#include "fs/filesystem.h"
#include "fat32driver.h"
#include "fs/storage/journal.h"

void LocateFilesystemsFAT32(Device* device);

struct FAT32_ActiveFile : public ActiveFile
{
    virtual uint64_t GetSize() const override;
    virtual const char* GetName() const override;
    virtual const char* GetPath() const override;

    virtual uint32_t GetAttributes() const override;

    FilePermissions GetPermissions() const override;

    DirEntry entry;
    const char* path = nullptr;
};

class FAT32 : public Filesystem
{
  public:
    FAT32(Device* device, uint32_t part_start);
    ~FAT32();

    virtual ActiveFile* OpenFile(const char* path) override;
    virtual void CloseFile(ActiveFile* file) override;

    virtual ActiveFile* CreateFile(const char* path, const char* name, permissions_t permissions) override;
    virtual int ResizeFile(ActiveFile* file, size_t newSize) override;
    virtual int DeleteFile(ActiveFile* file) override;

    virtual uint64_t GetDirectoryEntryCount(const char* path) override;
    virtual ActiveFile* GetFile(const char* path, uint64_t index) override;

    int Lookup(const char* path, ActiveFile*& file) override;
    int MakeDirectory(const char* path, const FilePermissions* permissions = nullptr) override;
    int RemoveFile(const char* path) override;
    int MoveFile(const char* source, const char* destination) override;
    int SaveFile(const char* path, const void* data, uint32_t size, bool create,
                 const FilePermissions* permissions = nullptr) override;

    bool AccountStorage(uint64_t (&sectors)[2][16], Device*& storage) override;
    virtual int Read(ActiveFile* file, void* buffer, uint64_t size) override;
    virtual int Write(ActiveFile* file, void* buffer, uint64_t size) override;

    int SetPermissions(const char* path, const FilePermissions& permissions) override;
    int Flush() override;
    int ReadDirectory(const char* path, uint64_t index, HackosFileInfo* output, uint64_t capacity,
                      uint64_t& count) override;
    int DirectoryCount(const char* path, uint64_t& count) override;

  private:
    int Finish(int status);
    Device* backing;
    uint32_t start;
    JournalDevice* journal;
    FAT32Driver* driver;
};

#endif
