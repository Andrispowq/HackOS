#ifndef FAT32_DRIVER_H
#define FAT32_DRIVER_H

#include "fat32defs.h"
#include "lib/memory.h"
#include "lib/data_structures/vector.h"

#include "drivers/device/device.h"
#include "arch/x86_64/timer/rtc.h"

class FAT32Driver
{
public:
    FAT32Driver(Device* device, uint32_t part_start);
    ~FAT32Driver();

	vector<DirEntry> GetDirectories(uint32_t cluster, uint32_t filter_attributes, bool exclude);

	void ModifyDirectoryEntry(uint32_t cluster, const char* name, DirEntry modified);

	int PrepareAddedDirectory(uint32_t cluster);
	void CleanFileEntry(uint32_t cluster, DirEntry entry);
	
	int DirectorySearch(const char* FilePart, uint32_t cluster, DirEntry* file);
	int DirectoryAdd(uint32_t cluster, DirEntry file);

	int OpenFile(const char* filePath, DirEntry* fileMeta);
	int CreateFile(const char* filePath, DirEntry* fileMeta);
	int DeleteFile(DirEntry fileMeta);

	int ReadFile(DirEntry fileMeta, uint64_t offset, void* buffer, uint64_t bytes);
	int WriteFile(DirEntry fileMeta, uint64_t offset, void* buffer, uint64_t bytes);
	int ResizeFile(DirEntry fileMeta, uint32_t new_size);

	uint32_t GetRootDirStart() const { return RootDirStart; }

	static vector<FAT32Driver*> LocateFilesystemsFAT32(Device* device);

private:
	uint32_t ReadFAT(uint32_t cluster);
	uint32_t WriteFAT(uint32_t cluster, uint32_t value);

	uint32_t ReadCluster(uint32_t cluster, void* buffer);
	uint32_t WriteCluster(uint32_t cluster, void* buffer);

	vector<uint32_t> GetClusterChain(uint32_t start);

	uint32_t AllocateClusterChain(uint32_t size);
	void FreeClusterChain(uint32_t start);

	void* ReadClusterChain(uint32_t start, uint32_t& size);
	void WriteClusterChain(uint32_t start, void* buffer, uint32_t size);
	void ResizeClusterChain(uint32_t start, uint32_t new_size);

	void GetDirectoriesOnCluster(uint32_t cluster, vector<DirEntry>& entries);
	
private:
	uint32_t GetClusterFromFilePath(const char* filePath, DirEntry* entry);

	DirEntry FromFATEntry(DirectoryEntry* entry, bool long_fname);
	DirectoryEntry* ToFATEntry(DirEntry entry, uint32_t& longEntries);

	bool Compare(DirectoryEntry* entry, const char* name, bool long_name);

	void ConvertFromFATFormat(char* input, char* output);
	int IsFATFormat(char* name);
	char* ConvertToFATFormat(char* input);

	uint8_t GetMilliseconds() const;
	uint16_t GetTime() const;
	uint16_t GetDate() const;

private:
	Device* device;
    uint32_t PartitionStart;

    FAT32_BootSector* BootSector;

    uint8_t* temporaryBuffer;
    uint8_t* temporaryBuffer2;
	uint32_t* FATcache;

	uint32_t ClusterSize;

    uint32_t FirstDataSector;
    uint32_t RootDirStart;

    uint32_t TotalSectors;
    uint32_t TotalClusters;
};

#endif