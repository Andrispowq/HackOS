#ifndef FAT32_DEFS_H
#define FAT32_DEFS_H

#include "lib/stdint.h"

#define END_CLUSTER 0x0FFFFFF8
#define BAD_CLUSTER 0x0FFFFFF7
#define FREE_CLUSTER 0x00000000

#define FILE_READ_ONLY 0x01
#define FILE_HIDDEN 0x02
#define FILE_SYSTEM 0x04
#define FILE_VOLUME_ID 0x08
#define FILE_DIRECTORY 0x10
#define FILE_ARCHIVE 0x20
#define FILE_LONG_NAME (FILE_READ_ONLY | FILE_HIDDEN | FILE_SYSTEM | FILE_VOLUME_ID)

#define ENTRY_FREE 0xE5
#define ENTRY_END 0x00
#define LAST_LONG_ENTRY 0x40

#define MAX_ENTRIES_IN_DIRECTORY 32

#define FAT32_SUCCESS 0
#define FAT32_ERROR_BAD_CLUSTER_VALUE -1
#define FAT32_ERROR_FILE_NOT_FOUND -2
#define FAT32_ERROR_NO_FREE_SPACE -3
#define FAT32_ERROR_INVALID_ARGUMENTS -4
#define FAT32_ERROR_NOT_DIRECTORY -5
#define FAT32_ERROR_DIRECTORY -6
#define FAT32_ERROR_NOT_FAT_NAME -7
#define FAT32_ERROR_LOWERCASE_ISSUE -8
#define FAT32_ERROR_NOT_CONVERTED_YET -9
#define FAT32_ERROR_BAD_CHARACTER -10

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

struct DirEntry
{
	char name[128];
	uint32_t cluster;
	uint32_t size;
	uint8_t attributes;

	uint32_t parentCluster;
	uint32_t offsetInParentCluster; 
};

#endif