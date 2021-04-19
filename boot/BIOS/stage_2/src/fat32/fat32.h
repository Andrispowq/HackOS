#ifndef FAT32_H
#define FAT32_H

#include "libc/stdint.h"
#include "drivers/ata/ata.h"

#define END_CLUSTER 0x0FFFFFF8
#define BAD_CLUSTER 0x0FFFFFF7
#define FREE_CLUSTER 0x00000000

#define FILE_DIRECTORY 0x10

#define ENTRY_END 0x00

#define LOWERCASE_ISSUE	0x01 //E.g.: "test    txt"
#define BAD_CHARACTER	0x02 //E.g.: "tes&t   txt"
#define BAD_TERMINATION 0x04 //missing null character at the end
#define NOT_CONVERTED_YET 0x08 //still contains a dot: E.g."test.txt"
#define TOO_MANY_DOTS 0x10 //E.g.: "test..txt"; may or may not have already been converted

typedef struct fat32_bootsect
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
} __attribute__((packed)) FAT32BootSector;

typedef struct fs_info
{
	uint32_t LeadSignature; //0x41615252
	uint8_t Reserved0[480];
	uint32_t StructSignature; //0x61417272
	uint32_t FreeSpace; //Last free cluster count (0xFFFFFFFF -> unknown)
	uint32_t LastWritten; //Last written cluster number, or 0xFFFFFFFF
	uint8_t Reserved1[12];
	uint32_t TrailSignature; //0xAA550000
} __attribute__((packed)) FSInfo;

typedef struct direntry
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
} __attribute__((packed)) DirectoryEntry;

typedef struct long_direntry
{
	uint8_t order;
	uint16_t name0[5];
	uint8_t attributes;
	uint8_t type;
	uint8_t checksum;
	uint16_t name1[6];
	uint16_t reserved;
	uint16_t name2[2];
} __attribute__((packed)) LongDirectoryEntry;

void InitialiseFAT();

int ReadFAT(uint32_t cluster);
int ReadCluster(uint32_t cluster, void* buffer);

int DirectorySearch(const char* filePart, uint32_t cluster, DirectoryEntry* file);
int GetFile(const char* path, void** fileContents, DirectoryEntry* fileMeta);

int IsFATFormat(char* name);
void ConvertFromFATFormat(char* input, char* output);
char* ConvertToFATFormat(char* input);

#endif