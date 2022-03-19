#include "fat32driver.h"

#include "lib/memory.h"
#include "lib/string.h"
#include "lib/stdio.h"

#include "drivers/ata/ata.h"

#include "fs/filesystem.h"

#include "arch/x86_64/paging/page_table_manager.h"

extern uint8_t fromUEFI;
extern PageTableManager KernelDirectory;

FAT32Driver::FAT32Driver(Device* device, uint32_t part_start)
	: device(device), PartitionStart(part_start)
{
    //The MBR is still at 0x0600, extract the boot partition's start LBA
    uint32_t* MBR = (uint32_t*)0x0600;

	//If we don't find any partition tables valid, we should assume that we aren't partitioned
	PartitionStart = 0;

	//On UEFI, we don't have partitions for now
	if(!fromUEFI)
	{
    	uint64_t BaseOffset = 446;
		Partition* part = (Partition*)((uint64_t)MBR + BaseOffset + 16); //the 2nd partition contains the FAT32 filesystem 
		PartitionStart = part->LBA_start;
	}

    //Load the bootsector
    BootSector = (FAT32_BootSector*)kmalloc(512);
    device->Read(PartitionStart, (void*)BootSector, 1);

    kprintf("Read a bootsector from drive with a FAT32 filesystem on it!\n");

    FirstDataSector = BootSector->NumberOfFATs * BootSector->SectorsPerFAT32 + BootSector->ReservedSectors;
    RootDirStart = BootSector->RootDirStart;

    if(BootSector->TotalSectors == 0)
    {
        TotalSectors = BootSector->LargeTotalSectors;
    }
    else
    {
        TotalSectors = BootSector->TotalSectors;
    }

    TotalClusters = TotalSectors / BootSector->SectorsPerCluster;
	ClusterSize = BootSector->SectorsPerCluster * BootSector->BytesPerSector;

    temporaryBuffer = (uint8_t*)kmalloc(ClusterSize);
    temporaryBuffer2 = (uint8_t*)kmalloc(ClusterSize);
	FATcache = (uint32_t*)kmalloc(BootSector->SectorsPerFAT32 * BootSector->BytesPerSector);
	device->Read(PartitionStart + BootSector->ReservedSectors, FATcache, BootSector->SectorsPerFAT32);

    kprintf("Statistics: \n");
    kprintf("\tFirst data sector: %d\n", FirstDataSector);
    kprintf("\tRoot directory start cluster: %d\n", RootDirStart);
    kprintf("\tMedia size in sectors: %d\n", TotalSectors);
    kprintf("\tNumber of clusters: %d\n", TotalClusters);
    kprintf("\tSize of clusters: %d\n\n", ClusterSize);
}

FAT32Driver::~FAT32Driver()
{
	device->Write(PartitionStart, BootSector, 1);
	device->Write(PartitionStart + BootSector->BackupBootSector, BootSector, 1);

	//copy the cached FAT onto every one upon closing the FS
	for (uint32_t i = 0; i < BootSector->NumberOfFATs; i++)
	{
		device->Write(PartitionStart + BootSector->ReservedSectors + BootSector->SectorsPerFAT32 * i, FATcache, BootSector->SectorsPerFAT32);
	}

    kfree(temporaryBuffer);
    kfree(temporaryBuffer2);
	kfree(FATcache);
	kfree(BootSector);
}

uint32_t FAT32Driver::ReadFAT(uint32_t cluster)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return FAT32_ERROR_BAD_CLUSTER_VALUE;
	}

	return FATcache[cluster] & 0x0FFFFFFF;
}

uint32_t FAT32Driver::WriteFAT(uint32_t cluster, uint32_t value)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return FAT32_ERROR_BAD_CLUSTER_VALUE;
	}

	FATcache[cluster] &= 0xF0000000;
	FATcache[cluster] |= (value & 0x0FFFFFFF);
	return 0;
}

uint32_t FAT32Driver::ReadCluster(uint32_t cluster, void* buffer)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}

	uint32_t start_sector = (cluster - 2) * BootSector->SectorsPerCluster + FirstDataSector;
	device->Read(PartitionStart + start_sector, buffer, ClusterSize / BootSector->BytesPerSector);
	return 0;
}

uint32_t FAT32Driver::WriteCluster(uint32_t cluster, void* buffer)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}

	uint32_t start_sector = (cluster - 2) * BootSector->SectorsPerCluster + FirstDataSector;
	device->Write(PartitionStart + start_sector, buffer, ClusterSize / BootSector->BytesPerSector);
	return 0;
}

vector<uint32_t> FAT32Driver::GetClusterChain(uint32_t start)
{
	if (start < 2 || start > TotalClusters)
	{
		return {};
	}

	vector<uint32_t> chain;
	chain.push_back(start);

	uint32_t current = start;
	while (true)
	{
		current = ReadFAT(current);
		if (current == BAD_CLUSTER)
		{
			return {};
		}

		if (current < END_CLUSTER)
		{
			chain.push_back(current);
		}
		else
		{
			break;
		}
	}

	return chain;
}

uint32_t FAT32Driver::AllocateClusterChain(uint32_t size)
{
	uint32_t totalAllocated = 0;

	uint32_t cluster = 2;
	uint32_t prevCluster = cluster;
	uint32_t firstCluster = 0;
	uint32_t clusterStatus = FREE_CLUSTER;

	while (totalAllocated < size)
	{
		if (cluster >= TotalClusters)
		{
			return BAD_CLUSTER;
		}

		clusterStatus = ReadFAT(cluster);
		if (clusterStatus == FREE_CLUSTER)
		{
			if (totalAllocated != 0)
			{
				if (WriteFAT(prevCluster, cluster) != 0)
				{
					return BAD_CLUSTER;
				}
			}
			else
			{
				firstCluster = cluster;
			}
			
			if (totalAllocated == (size - 1))
			{
				if (WriteFAT(cluster, END_CLUSTER) != 0)
				{
					return BAD_CLUSTER;
				}
			}

			totalAllocated++;
			prevCluster = cluster;
		}

		cluster++;
	}

	return firstCluster;
}

void FAT32Driver::FreeClusterChain(uint32_t start)
{
	vector<uint32_t> chain = GetClusterChain(start);

	for (uint32_t i = 0; i < chain.size(); i++)
	{
		WriteFAT(chain[i], FREE_CLUSTER);
	}
}

void* FAT32Driver::ReadClusterChain(uint32_t start, uint32_t& size)
{
	if (start < 2 || start > TotalClusters)
	{
		return {};
	}

	vector<uint32_t> chain = GetClusterChain(start);
	uint32_t size_ = chain.size();
	size = size_ * ClusterSize;

	uint8_t* buffer = new uint8_t[size];

	for (uint32_t i = 0; i < size; i++)
	{
		ReadCluster(chain[i], buffer);
		buffer += ClusterSize;
	}

	return (void*)buffer;
}

void FAT32Driver::WriteClusterChain(uint32_t start, void* buffer, uint32_t size)
{
	vector<uint32_t> chain = GetClusterChain(start);
	uint32_t totalSize = chain.size();
	uint32_t writeSize = size;
	if (writeSize > totalSize)
	{
		writeSize = totalSize;
	}

	uint8_t* buf = (uint8_t*)buffer;
	for (uint32_t i = 0; i < writeSize; i++)
	{
		WriteCluster(chain[i], buf);
		buf += ClusterSize;
	}
}

void FAT32Driver::ResizeClusterChain(uint32_t start, uint32_t new_size)
{
	vector<uint32_t> chain = GetClusterChain(start);
	uint32_t cur_size = chain.size();

	if (cur_size == new_size)
	{
		return;
	}
	else if (cur_size > new_size)
	{
		WriteFAT(chain[new_size - 1], END_CLUSTER);
		FreeClusterChain(chain[new_size]);
	}
	else
	{
		uint32_t start_of_the_rest = AllocateClusterChain(new_size - cur_size);
		WriteFAT(chain[cur_size - 1], start_of_the_rest);
	}
}

void FAT32Driver::GetDirectoriesOnCluster(uint32_t cluster, vector<DirEntry>& entries)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return;
	}

	ReadCluster(cluster, temporaryBuffer);

	DirectoryEntry* metadata = (DirectoryEntry*)temporaryBuffer;
	uint32_t meta_pointer_iterator = 0;

	bool LFN = false;
	while (true)
	{
		if (metadata->name[0] == ENTRY_END)
		{
			break;
		}
		else if ((metadata->name[0] == (char)ENTRY_FREE) || ((metadata->attributes & FILE_LONG_NAME) == FILE_LONG_NAME))
		{
			LFN = ((metadata->attributes & FILE_LONG_NAME) == FILE_LONG_NAME);

			//If we are under the cluster limit
			if (meta_pointer_iterator < ClusterSize / sizeof(DirectoryEntry) - 1)
			{
				metadata++;
				meta_pointer_iterator++;
			}
			//Search next cluster
			else
			{
				uint32_t next_cluster = ReadFAT(cluster);
				if (next_cluster >= END_CLUSTER)
				{
					break;
				}
				else
				{
					//Search next cluster
					return GetDirectoriesOnCluster(next_cluster, entries);
				}
			}
		}
		else
		{
			DirEntry entry = FromFATEntry(metadata, LFN);
			entry.parentCluster = cluster;
			entry.offsetInParentCluster = meta_pointer_iterator;
			entries.push_back(entry);

			LFN = false;

			metadata++;
			meta_pointer_iterator++;
		}
	}
}

vector<DirEntry> FAT32Driver::GetDirectories(uint32_t cluster, uint32_t filter_attributes, bool exclude)
{
	vector<DirEntry> ret;
	if (cluster < 2 || cluster > TotalClusters)
	{
		return ret;
	}

	vector<DirEntry> all;
	GetDirectoriesOnCluster(cluster, all);

	for (size_t i = 0; i < all.size(); i++)
	{
		if (!exclude)
		{
			if (all[i].attributes & filter_attributes)
			{
				continue;
			}

			ret.push_back(all[i]);
		}
		else
		{
			if (all[i].attributes & filter_attributes)
			{
				ret.push_back(all[i]);
			}
		}
	}

	return ret;
}

void FAT32Driver::ModifyDirectoryEntry(uint32_t cluster, const char* name, DirEntry modified)
{
	bool isLFN = false;
	if (IsFATFormat((char*)name) != 0)
	{
		isLFN = true;
	}

	ReadCluster(cluster, temporaryBuffer);

	DirectoryEntry* metadata = (DirectoryEntry*)temporaryBuffer;
	uint32_t meta_pointer_iterator = 0;

	uint32_t count = 0;
	DirectoryEntry* ent = ToFATEntry(modified, count);

	while (1)
	{
		if (metadata->name[0] == ENTRY_END)
		{
			break;
		}
		else if (((metadata->attributes & FILE_LONG_NAME) == FILE_LONG_NAME) || !(Compare(metadata, name, isLFN)))
		{
			//If we are under the cluster limit
			if (meta_pointer_iterator < ClusterSize / sizeof(DirectoryEntry) - 1)
			{
				metadata++;
				meta_pointer_iterator++;
			}
			//Search next cluster
			else
			{
				uint32_t next_cluster = ReadFAT(cluster);
				if (next_cluster >= END_CLUSTER)
				{
					break;
				}
				else
				{
					//Search next cluster
					return ModifyDirectoryEntry(next_cluster, name, modified);
				}
			}
		}
		else
		{
			if (modified.attributes == 0)
			{
				//We want to delete the entry altogether
				for (uint32_t i = 0; i < (count + 1); i++)
				{
					DirectoryEntry* curr = metadata - i;
					curr->name[0] = ENTRY_FREE;
				}
			}
			else
			{
				metadata->attributes = modified.attributes;
				metadata->clusterLow = modified.cluster & 0xFFFF;
				metadata->clusterHigh = (modified.cluster << 16) & 0xFFFF;
				metadata->fileSize = modified.size;
			}

			WriteCluster(cluster, temporaryBuffer);
			break;
		}
	}
}

int FAT32Driver::PrepareAddedDirectory(uint32_t cluster)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}

	char* tempBuff = new char[ClusterSize];
	ReadCluster(cluster, tempBuff);

	DirectoryEntry* metadata = (DirectoryEntry*)tempBuff;
	memset(metadata, 0, sizeof(DirectoryEntry));
	memcpy(metadata->name, ".          ", 11);
	metadata->attributes = FILE_DIRECTORY;
	metadata->clusterLow = cluster & 0xFFFF;
	metadata->clusterHigh = (cluster >> 16) & 0xFFFF;

	metadata->ctime_date = GetDate();
	metadata->ctime_time = GetTime();
	metadata->ctime_ms = GetMilliseconds();
	metadata->atime_date = GetDate();
	metadata->mtime_date = GetDate();
	metadata->mtime_time = GetTime();

	metadata++;
	memset(metadata, 0, sizeof(DirectoryEntry));
	memcpy(metadata->name, "..         ", 11);
	metadata->attributes = FILE_DIRECTORY;

	metadata->ctime_date = GetDate();
	metadata->ctime_time = GetTime();
	metadata->ctime_ms = GetMilliseconds();
	metadata->atime_date = GetDate();
	metadata->mtime_date = GetDate();
	metadata->mtime_time = GetTime();

	WriteCluster(cluster, tempBuff);
	delete[] tempBuff;

	return 0;
}

void FAT32Driver::CleanFileEntry(uint32_t cluster, DirEntry entry)
{
	entry.attributes = 0;
	ModifyDirectoryEntry(cluster, entry.name, entry);
}

int FAT32Driver::DirectorySearch(const char* FilePart, uint32_t cluster, DirEntry* file)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}

	vector<DirEntry> entries;
	GetDirectoriesOnCluster(cluster, entries);

	for (size_t i = 0; i < entries.size(); i++)
	{
		if (strcmp(entries[i].name, (char*)FilePart) == 0)
		{
			if (file != nullptr)
			{
				*file = entries[i];
			}

			return 0;
		}
	}

	return -2;
}

int FAT32Driver::DirectoryAdd(uint32_t cluster, DirEntry file)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}

	bool isLFN = false;
	if (IsFATFormat(file.name) != 0)
	{
		isLFN = true;
	}

	ReadCluster(cluster, temporaryBuffer);

	DirectoryEntry* metadata = (DirectoryEntry*)temporaryBuffer;
	uint32_t meta_pointer_iterator = 0;

	uint32_t count;
	DirectoryEntry* ent = ToFATEntry(file, count);

	uint32_t freeCount = 0;
	while (1)
	{
		if (((metadata->name[0] != (char)ENTRY_FREE) && (metadata->name[0] != ENTRY_END)))
		{
			if (meta_pointer_iterator < ClusterSize / sizeof(DirectoryEntry) - 1)
			{
				metadata++;
				meta_pointer_iterator++;
			}
			else
			{
				uint32_t next_cluster = ReadFAT(cluster);
				if (next_cluster >= END_CLUSTER)
				{
					next_cluster = AllocateClusterChain(1);
					if (next_cluster == BAD_CLUSTER)
					{
						return -1;
					}

					WriteFAT(cluster, next_cluster);
				}

				return DirectoryAdd(next_cluster, file);
			}
		}
		else
		{
			if (freeCount < count)
			{
				freeCount++;

				if (meta_pointer_iterator < ClusterSize / sizeof(DirectoryEntry) - 1)
				{
					metadata++;
					meta_pointer_iterator++;
					continue;
				}
				else
				{
					uint32_t next_cluster = ReadFAT(cluster);
					if (next_cluster >= END_CLUSTER)
					{
						next_cluster = AllocateClusterChain(1);
						if (next_cluster == BAD_CLUSTER)
						{
							return -1;
						}

						WriteFAT(cluster, next_cluster);
					}

					return DirectoryAdd(next_cluster, file);
				}
			}
			
			ent->ctime_date = GetDate();
			ent->ctime_time = GetTime();
			ent->ctime_ms = GetMilliseconds();
			ent->atime_date = GetDate();
			ent->mtime_date = GetDate();
			ent->mtime_time = GetTime();

			//For now we assume that the long entries fit in one cluster, let's hope it's true
			uint32_t clust_size = ent->fileSize / ClusterSize;
			if ((clust_size * ClusterSize) != ClusterSize)
			{
				clust_size++;
			}

			uint32_t new_cluster = AllocateClusterChain(clust_size);
			if (new_cluster == BAD_CLUSTER)
			{
				return -1;
			}

			char* buffer = new char[clust_size * ClusterSize];
			memset(buffer, 0, clust_size * ClusterSize);
			WriteClusterChain(new_cluster, buffer, clust_size * ClusterSize);

			if ((ent->attributes & FILE_DIRECTORY) == FILE_DIRECTORY)
			{
				PrepareAddedDirectory(new_cluster);
			}

			ent->clusterLow = new_cluster & 0xFFFF;
			ent->clusterHigh = (new_cluster >> 16) & 0xFFFF;

			memcpy(metadata - count, ent - count, sizeof(DirectoryEntry) * (count + 1));
			WriteCluster(cluster, temporaryBuffer); //Write the modified stuff back

			return 0;
		}
	}
	return -1;
}

int FAT32Driver::OpenFile(const char* filePath, DirEntry* fileMeta)
{
	if (fileMeta == nullptr)
	{
		return -1;
	}

	int ret = GetClusterFromFilePath(filePath, fileMeta);
	if(ret < 0)
	{
		return ret;
	}

	return 0;
}

int FAT32Driver::CreateFile(const char* filePath, DirEntry* fileMeta)
{
	DirEntry parentInfo;
	uint32_t active_cluster = GetClusterFromFilePath(filePath, &parentInfo);

	fileMeta->parentCluster = active_cluster;
	fileMeta->offsetInParentCluster = -1;

	//Makes sure there's no other file like this
	int retVal = DirectorySearch(fileMeta->name, active_cluster, nullptr);
	if (retVal != -2)
	{
		return retVal;
	}

	//Not the function to create a directory
	if ((parentInfo.attributes & FILE_DIRECTORY) != FILE_DIRECTORY)
	{
		return -2;
	}

	retVal = DirectoryAdd(active_cluster, *fileMeta);
	if(retVal != 0)
	{
		return -1;
	}

	return DirectorySearch(fileMeta->name, active_cluster, fileMeta);
}

int FAT32Driver::DeleteFile(DirEntry entry)
{
	if ((entry.attributes & FILE_DIRECTORY) == FILE_DIRECTORY)
	{
		vector<DirEntry> subDirs = GetDirectories(entry.cluster, 0, false);

		for (size_t i = 0; i < subDirs.size(); i++)
		{
			//We can't delete the '.' and '..' entries
			if(subDirs[i].name[0] != '.')
			{
				DeleteFile(subDirs[i]);
			}
		}
	}

	FreeClusterChain(entry.cluster);
	CleanFileEntry(entry.parentCluster, entry);

	return 0;
}

int FAT32Driver::ReadFile(DirEntry fileMeta, uint64_t offset, void* buffer, uint64_t bytes)
{
	if ((fileMeta.attributes & FILE_DIRECTORY) == FILE_DIRECTORY)
	{
		return -3;
	}

	if ((bytes + offset) > fileMeta.size)
	{
		bytes = fileMeta.size - offset;
	}

	vector<uint32_t> chain = GetClusterChain(fileMeta.cluster);

	uint64_t bytes_so_far = 0;
	uint8_t* buff = (uint8_t*)buffer;
	for (uint32_t i = 0; i < chain.size(); i++)
	{
		uint32_t clus = chain[i];

		if (bytes_so_far < offset)
		{
			bytes_so_far += ClusterSize;
			continue;
		}

		uint64_t toRead = (bytes + offset) - bytes_so_far;
		if (toRead > ClusterSize)
		{
			toRead = ClusterSize;
			ReadCluster(clus, buff);
		}
		else
		{
			uint8_t* temporary = new uint8_t[ClusterSize];
			ReadCluster(clus, temporary);
			memcpy(buff, temporary, toRead);
			delete[] temporary;
		}

		buff += ClusterSize;
		bytes_so_far += toRead;
	}

	return 0;
}

int FAT32Driver::WriteFile(DirEntry fileMeta, uint64_t offset, void* buffer, uint64_t bytes)
{
	if ((fileMeta.attributes & FILE_DIRECTORY) == FILE_DIRECTORY)
	{
		return -3;
	}

	if ((bytes + offset) > fileMeta.size)
	{
		fileMeta.size = bytes + offset;

		uint32_t new_cluster_size = fileMeta.size / ClusterSize;
		if ((new_cluster_size * ClusterSize) != fileMeta.size)
		{
			new_cluster_size++;
		}

		ResizeClusterChain(fileMeta.cluster, new_cluster_size);
	}

	vector<uint32_t> chain = GetClusterChain(fileMeta.cluster);

	uint64_t bytes_so_far = 0;
	uint8_t* buff = (uint8_t*)buffer;
	for (uint32_t i = 0; i < chain.size(); i++)
	{
		uint32_t clus = chain[i];

		if (bytes_so_far < offset)
		{
			bytes_so_far += ClusterSize;
			continue;
		}

		uint64_t toWrite = (bytes + offset) - bytes_so_far;
		if (toWrite > ClusterSize)
		{
			toWrite = ClusterSize;
			WriteCluster(clus, buff);
		}
		else
		{
			uint8_t* temporary = new uint8_t[ClusterSize];
			memset(temporary, 0, ClusterSize);
			memcpy(temporary, buff, toWrite);
			WriteCluster(clus, temporary);
			delete[] temporary;
		}

		buff += ClusterSize;
		bytes_so_far += toWrite;
	}

	ModifyDirectoryEntry(fileMeta.parentCluster, fileMeta.name, fileMeta);
	return 0;
}

int FAT32Driver::ResizeFile(DirEntry fileMeta, uint32_t new_size)
{
	if ((fileMeta.attributes & FILE_DIRECTORY) == FILE_DIRECTORY)
	{
		return -3;
	}

	fileMeta.size = new_size;

	uint32_t new_cluster_size = fileMeta.size / ClusterSize;
	if ((new_cluster_size * ClusterSize) != fileMeta.size)
	{
		new_cluster_size++;
	}

	ResizeClusterChain(fileMeta.cluster, new_cluster_size);
	ModifyDirectoryEntry(fileMeta.parentCluster, fileMeta.name, fileMeta);

	return 0;
}

uint32_t FAT32Driver::GetClusterFromFilePath(const char* filePath, DirEntry* entry)
{
	char fileNamePart[256];
	uint16_t start = 2; //Skip the "~/" part
	uint32_t active_cluster = RootDirStart;
	DirEntry fileInfo;

	uint32_t iterator = 2;
	if (strcmp((char*)filePath, "~") == 0)
	{
		fileInfo.attributes = FILE_DIRECTORY | FILE_VOLUME_ID;
		fileInfo.size = 0;
		fileInfo.cluster = active_cluster;
	}
	else
	{
		for (iterator = 2; filePath[iterator - 1] != 0; iterator++)
		{
			if (filePath[iterator] == '/' || filePath[iterator] == 0)
			{
				memset(fileNamePart, 0, 256);
				memcpy(fileNamePart, (void*)((uint64_t)filePath + start), iterator - start);

				int retVal = DirectorySearch(fileNamePart, active_cluster, &fileInfo);

				if (retVal != 0)
				{
					return retVal;
				}

				start = iterator + 1;
				active_cluster = fileInfo.cluster;
			}
		}
	}

	if (entry)
	{
		*entry = fileInfo;
	}

	return active_cluster;
}

DirEntry FAT32Driver::FromFATEntry(DirectoryEntry* entry, bool long_fname)
{
	DirEntry ent;

	if (long_fname)
	{
		char long_name[128];
		uint32_t count = 0;
		LongDirectoryEntry* lEntry = (LongDirectoryEntry*)(entry - 1);
		while (true)
		{
			for (uint32_t i = 0; i < 5; i++)
			{
				long_name[count++] = (char)lEntry->name0[i];
			}
			for (uint32_t i = 0; i < 6; i++)
			{
				long_name[count++] = (char)lEntry->name1[i];
			}
			for (uint32_t i = 0; i < 2; i++)
			{
				long_name[count++] = (char)lEntry->name2[i];
			}

			if ((lEntry->order & LAST_LONG_ENTRY) == LAST_LONG_ENTRY)
			{
				break;
			}

			lEntry--;
		}

		memcpy(ent.name, long_name, count);
		ent.name[count] = 0;
	}
	else
	{
		char out[13];
		ConvertFromFATFormat(entry->name, out);
		strcpy(ent.name, out);

		for (uint32_t i = 11; i >= 0; i--)
		{
			if (ent.name[i] == ' ')
			{
				ent.name[i] = 0;
			}
			else
			{
				break;
			}
		}
	}

	ent.size = entry->fileSize;
	ent.attributes = entry->attributes;
	ent.cluster = entry->clusterLow | (entry->clusterHigh << 16);

	return ent;
}

DirectoryEntry* FAT32Driver::ToFATEntry(DirEntry entry, uint32_t& longEntries)
{
	char* namePtr = entry.name;
	size_t nameLen = strlen(namePtr);
	if (nameLen <= 12)
	{
		char* fat_name = nullptr;
		if ((strncmp(namePtr, ".          ", 11) == 0) || (strncmp(namePtr, "..         ", 11) == 0))
		{
			fat_name = namePtr;
		}
		else
		{
			fat_name = ConvertToFATFormat(namePtr);
		}

		longEntries = 0;

		DirectoryEntry* ent = new DirectoryEntry();
		memset(ent, 0, sizeof(DirectoryEntry));
		memcpy(ent->name, fat_name, 11);
		ent->attributes = entry.attributes;
		ent->fileSize = entry.size;
		ent->clusterLow = entry.cluster & 0xFFFF;
		ent->clusterHigh = (entry.cluster >> 16) & 0xFFFF;
		return ent;

		delete[] fat_name;
	}
	else
	{
		size_t long_entries = (nameLen / 13) + 1;
		longEntries = (uint32_t)long_entries;
		LongDirectoryEntry* entries = new LongDirectoryEntry[long_entries + 1];
		LongDirectoryEntry* start = entries + (long_entries - 1);

		char shortName[11];
		for (uint32_t i = 0; i < 6; i++)
		{
			char ch = namePtr[i];
			if((ch >= 'a') && (ch <= 'z'))
			{
				shortName[i] = ch + ('A' - 'a');
			}
		}

		shortName[6] = '~';
		shortName[7] = '1';

		bool hasEXT = false;
		for (uint32_t i = 6; i < nameLen; i++)
		{
			if (namePtr[i] == '.')
			{
				hasEXT = true;
				for (uint32_t j = 0; (j < (nameLen - (i + 1))) && (j < 3); j++)
				{
					char ch = namePtr[i + 1 + j];
					if((ch >= 'a') && (ch <= 'z'))
					{
						shortName[i] = ch + ('A' - 'a');
					}
				}
				break;
			}
		}

		if (!hasEXT)
		{
			shortName[8] = ' ';
			shortName[9] = ' ';
			shortName[10] = ' ';
		}

		uint8_t checksum = 0;

		for (size_t i = 11; i; i--)
		{
			checksum = ((checksum & 1) << 7) + (checksum >> 1) + shortName[11 - i];
		}

		uint32_t counter = 0;
		for (size_t j = 0; j < long_entries; j++)
		{
			memset(start, 0, sizeof(LongDirectoryEntry));

			start->order = uint8_t(j + 1);
			if (j == (long_entries - 1))
			{
				start->order |= 0x40;
			}

			start->attributes = FILE_LONG_NAME;
			start->checksum = checksum;

			for (uint32_t i = 0; i < 5; i++)
			{
				if (counter >= nameLen)
				{
					start->name0[i] = 0;
					break;
				}

				start->name0[i] = (uint16_t)namePtr[counter++];
			}
			for (uint32_t i = 0; i < 6; i++)
			{
				if (counter >= nameLen)
				{
					start->name1[i] = 0;
					break;
				}

				start->name1[i] = (uint16_t)namePtr[counter++];
			}
			for (uint32_t i = 0; i < 2; i++)
			{
				if (counter >= nameLen)
				{
					start->name2[i] = 0;
					break;
				}

				start->name2[i] = (uint16_t)namePtr[counter++];
			}

			start--;
		}

		DirectoryEntry* dirEntry = (DirectoryEntry*)(entries + long_entries);
		memset(dirEntry, 0, sizeof(DirectoryEntry));
		memcpy(dirEntry->name, shortName, 11);
		dirEntry->attributes = entry.attributes;
		dirEntry->fileSize = entry.size;
		dirEntry->clusterLow = entry.cluster & 0xFFFF;
		dirEntry->clusterHigh = (entry.cluster >> 16) & 0xFFFF;

		return dirEntry;
	}

	return nullptr;
}

bool FAT32Driver::Compare(DirectoryEntry* entry, const char* name, bool long_name)
{
	DirEntry ent = FromFATEntry(entry, long_name);
	return strcmp(ent.name, (char*)name) == 0;
}

uint8_t FAT32Driver::GetMilliseconds() const
{
	return 0; //Currently not implemented
}

uint16_t FAT32Driver::GetTime() const
{
	tm time;
	GetTimeRTC(&time);

	uint16_t sec_over_2 = time.second / 2;
	uint16_t min = time.minute;
	uint16_t hour = time.hour;

	return sec_over_2 | (min << 5) | (hour << 11);
}

uint16_t FAT32Driver::GetDate() const
{
	tm time;
	GetTimeRTC(&time);

	uint16_t day = time.day;
	uint16_t mon = time.month + 1;
	uint16_t year = time.year + 1900;

	return day | (mon << 5) | ((year - 1980) << 9);
}

int FAT32Driver::IsFATFormat(char* name)
{
	size_t len = strlen(name);
	if (len != 11)
	{
		return FAT32_ERROR_NOT_CONVERTED_YET;
	}

	if ((strncmp(name, ".          ", 11) == 0) || (strncmp(name, "..         ", 11) == 0))
	{
		return 0;
	}

	int retVal = 0;

	uint32_t iterator;
	for (iterator = 0; iterator < 11; iterator++)
	{
		if (name[iterator] < 0x20 && name[iterator] != 0x05)
		{
			retVal = retVal | FAT32_ERROR_BAD_CHARACTER;
		}

		switch (name[iterator])
		{
		case 0x2E:
		case 0x22:
		case 0x2A:
		case 0x2B:
		case 0x2C:
		case 0x2F:
		case 0x3A:
		case 0x3B:
		case 0x3C:
		case 0x3D:
		case 0x3E:
		case 0x3F:
		case 0x5B:
		case 0x5C:
		case 0x5D:
		case 0x7C:
			retVal = retVal | FAT32_ERROR_BAD_CHARACTER;
		}

		if (name[iterator] >= 'a' && name[iterator] <= 'z')
		{
			retVal = retVal | FAT32_ERROR_LOWERCASE_ISSUE;
		}
	}

	return retVal;
}

char* FAT32Driver::ConvertToFATFormat(char* input)
{
	uint32_t counter = 0;

	for (uint32_t i = 0; i < 12; i++)
	{
		char ch = input[i];
		if((ch >= 'a') && (ch <= 'z'))
		{
			input[i] = ch + ('A' - 'a');
		}
	}

	char searchName[13] = { '\0' };
	uint16_t dotPos = 0;
	bool hasEXT = false;

	counter = 0;
	while (counter <= 8) //copy all the characters from filepart into searchname until a dot or null character is encountered
	{
		if (input[counter] == '.' || input[counter] == '\0')
		{
			dotPos = counter;

			if (input[counter] == '.')
			{
				counter++; //iterate off dot
				hasEXT = true;
			}

			break;
		}
		else
		{
			searchName[counter] = input[counter];
			counter++;
		}
	}

	if (counter > 9) //a sanity check in case there was a dot-less 11 character filename
	{
		counter = 8;
		dotPos = 8;
	}

	uint16_t extCount = 8;
	while (extCount < 11) //add the extension to the end, putting spaces where necessary
	{
		if (input[counter] != '\0' && hasEXT)
			searchName[extCount] = input[counter];
		else
			searchName[extCount] = ' ';

		counter++;
		extCount++;
	}

	counter = dotPos; //reset counter to position of the dot

	while (counter < 8) //if the dot is within the 8 character limit of the name, iterate through searchName putting in spaces up until the extension
	{
		searchName[counter] = ' ';
		counter++;
	}

	strcpy(input, searchName); //copy results back to input

	return input;
}

void FAT32Driver::ConvertFromFATFormat(char* input, char* output)
{
	//If the entry passed in is one of the dot special entries, just return them unchanged.
	if (input[0] == '.')
	{
		if (input[1] == '.')
		{
			strcpy(output, "..");
			return;
		}

		strcpy(output, ".");
		return;
	}

	uint16_t counter = 0;

	//iterate through the 8 letter file name, adding a dot when the end is reached
	for (counter = 0; counter < 8; counter++)
	{
		if (input[counter] == 0x20)
		{
			output[counter] = '.';
			break;
		}

		output[counter] = input[counter];
	}

	//if the entire 8 letters of the file name were used, tack a dot onto the end
	if (counter == 8)
	{
		output[counter] = '.';
	}

	uint16_t counter2 = 8;

	//iterate through the three-letter extension, adding it on. (Note: if the input is a directory (which has no extension) it erases the dot put in previously)
	for (counter2 = 8; counter2 < 11; counter2++)
	{
		++counter;
		if (input[counter2] == 0x20)
		{
			if (counter2 == 8) //there is no extension, the dot added earlier must be removed
				counter -= 2; //it's minus two because the for loop above iterates the loop as well
			break;
		}

		output[counter] = input[counter2];
	}

	++counter;
	while (counter < 12)
	{
		output[counter] = ' ';
		++counter;
	}

	output[12] = '\0'; //ensures proper termination regardless of program operation previously
	return;
}