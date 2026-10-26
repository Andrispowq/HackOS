#ifndef FAT32_DRIVER_H
#define FAT32_DRIVER_H

#include "fat32defs.h"
#include "lib/data_structures/vector.h"
#include "drivers/device/device.h"

class FAT32Driver
{
public:
    FAT32Driver(Device* device, uint32_t part_start);
    ~FAT32Driver();
    bool IsValid() const { return valid; }
    vector<DirEntry> GetDirectories(uint32_t cluster, uint32_t filter_attributes, bool exclude);
    int OpenFile(const char* path, DirEntry* entry);
    int CreateFile(const char* parentPath, DirEntry* entry);
    int DeleteFile(DirEntry entry);
    int ReadFile(DirEntry entry, uint64_t offset, void* buffer, uint64_t bytes);
    int WriteFile(DirEntry& entry, uint64_t offset, void* buffer, uint64_t bytes);
    int ResizeFile(DirEntry& entry, uint32_t size);
    uint32_t GetRootDirStart() const { return RootDirStart; }

private:
    bool ValidCluster(uint32_t cluster) const;
    uint32_t ReadFAT(uint32_t cluster) const;
    void WriteFAT(uint32_t cluster, uint32_t value);
    void FlushFAT();
    void ReadCluster(uint32_t cluster, void* buffer);
    void WriteCluster(uint32_t cluster, void* buffer);
    vector<uint32_t> GetClusterChain(uint32_t start);
    uint32_t AllocateClusterChain(uint32_t count);
    void FreeClusterChain(uint32_t start);
    int DirectorySearch(const char* name, uint32_t cluster, DirEntry* entry);
    int DirectoryAdd(uint32_t cluster, DirEntry& entry);
    int UpdateEntry(const DirEntry& entry, bool remove = false);

    Device* device;
    uint32_t PartitionStart;
    FAT32_BootSector* BootSector = nullptr;
    uint8_t* temporaryBuffer = nullptr;
    uint32_t* FATcache = nullptr;
    uint32_t ClusterSize = 0;
    uint32_t FirstDataSector = 0;
    uint32_t RootDirStart = 0;
    uint32_t TotalClusters = 0;
    bool valid = false;
    bool dirty = false;
};
#endif
