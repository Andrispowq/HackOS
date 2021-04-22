#ifndef FAT32_DRIVER_H
#define FAT32_DRIVER_H

#include "lib/stdint.h"
#include "drivers/device/device.h"

#define END_CLUSTER 0x0FFFFFF8
#define BAD_CLUSTER 0x0FFFFFF7
#define FREE_CLUSTER 0x00000000

#define HARD_ERR 0x04000000

#define FILE_READ_ONLY 0x01
#define FILE_HIDDEN 0x02
#define FILE_SYSTEM 0x04
#define FILE_VOLUME_ID 0x08
#define FILE_DIRECTORY 0x10
#define FILE_ARCHIVE 0x20
#define FILE_LONG_NAME (FILE_READ_ONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_VOLUME_ID)
#define FILE_LONG_NAME_MASK (FILE_READ_ONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_VOLUME_ID | FILE_DIRECTORY | FILE_ARCHIVE)
#define FILE_LAST_LONG_ENTRY 0x40
#define ENTRY_FREE 0xE5
#define ENTRY_END 0x00
#define LAST_LONG_ENTRY 0x40

#define LOWERCASE_ISSUE	0x01 //E.g.: "test    txt"
#define BAD_CHARACTER	0x02 //E.g.: "tes&t   txt"
#define BAD_TERMINATION 0x04 //missing null character at the end
#define NOT_CONVERTED_YET 0x08 //still contains a dot: E.g."test.txt"
#define TOO_MANY_DOTS 0x10 //E.g.: "test..txt"; may or may not have already been converted

struct FAT32_BootSector
{
    uint8_t JumpInstruction[3];
	uint8_t OEM[8];
	uint16_t BytesPerSector;
	uint8_t SectorsPerCluster;
	uint16_t ReservedSectors;
	uint8_t NumberOfFATs;
	uint16_t RootEntries;
	uint16_t TotalSectors;
	uint8_t MediaType;
	uint16_t SectorsPerFAT;
	uint16_t SectorsPerTrack;
	uint16_t HeadsPerCylinder;
	uint32_t HiddenSectors;
	uint32_t LargeTotalSectors;

	uint32_t SectorsPerFAT32;
	uint16_t Flags;
	uint16_t Version;
	uint32_t RootDirStart;
	uint16_t FSInfoSector;
	uint16_t BackupBootSector;

	uint32_t Reserved0;
	uint32_t Reserved1;
	uint32_t Reserved2;

	uint8_t DriveNumber;
	uint8_t Reserved3;
	uint8_t BootSignature;
	uint32_t VolumeSerial;
	uint8_t VolumeLabel[11];
	uint8_t FSName[8];

	uint8_t BootCode[420];
	uint8_t BootablePartitionSignature[2];
} __attribute__((packed));

struct FSInfo
{
	uint32_t LeadSignature; //0x41615252
	uint8_t Reserved0[480];
	uint32_t StructSignature; //0x61417272
	uint32_t FreeSpace; //Last free cluster count (0xFFFFFFFF -> unknown)
	uint32_t LastWritten; //Last written cluster number, or 0xFFFFFFFF
	uint8_t Reserved1[12];
	uint32_t TrailSignature; //0xAA550000
} __attribute__((packed));

struct DirectoryEntry
{
	char name[8];
	char ext[3];

	uint8_t attributes;
	uint8_t reserved;

	uint8_t ctime_ms;
	uint16_t ctime_time;
	uint16_t ctime_date;
	uint16_t atime_date;
	uint16_t clusterHigh;

	uint16_t mtime_time;
	uint16_t mtime_date;
	uint16_t clusterLow;
	uint32_t fileSize;
} __attribute__((packed));

struct LongDirectoryEntry
{
	uint8_t order;
	uint16_t name0[5];
	uint8_t attributes;
	uint8_t type;
	uint8_t checksum;
	uint16_t name1[6];
	uint16_t reserved;
	uint16_t name2[2];
} __attribute__((packed));

class FAT32Driver
{
public:
    FAT32Driver(Device* device);
    ~FAT32Driver();

    uint32_t ReadFAT(uint32_t cluster);
	uint32_t WriteFAT(uint32_t cluster, uint32_t value);

	uint32_t AllocateFreeFAT();

	uint32_t ReadCluster(uint32_t cluster, void* buffer);
	uint32_t WriteCluster(uint32_t cluster, void* buffer);

    DirectoryEntry* ListDirectory(uint32_t cluster, uint32_t attributes, bool exclusive, uint32_t* entryCount);
	
	int DirectorySearch(const char* FilePart, uint32_t cluster, DirectoryEntry* file, uint32_t* entryOffset);
	int DirectoryAdd(uint32_t cluster, DirectoryEntry* file, void* writeBuffer);

	int GetFile(const char* filePath, void** fileContents, DirectoryEntry* fileMeta);
	int PutFile(const char* filePath, void* fileContents, DirectoryEntry* fileMeta);

	uint32_t GetFirstDataSector() const { return FirstDataSector; }
	uint32_t GetRootDirStart() const { return RootDirStart; }

	uint32_t GetTotalClusters() const { return TotalClusters; }

	void ConvertFromFATFormat(char* input, char* output);

private:
	int IsFATFormat(char* name);
	char* ConvertToFATFormat(char* input);

	Device* device;
    FAT32_BootSector* BootSector;

    uint8_t* temporaryBuffer;
    uint8_t* temporaryBuffer2;

    uint64_t temporaryBufferSize;

    uint32_t PartitionStart;

    uint32_t FirstDataSector;
    uint32_t RootDirStart;

    uint32_t TotalSectors;
    uint32_t TotalClusters;
};

#endif