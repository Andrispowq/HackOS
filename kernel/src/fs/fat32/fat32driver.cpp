#include "fat32driver.h"

#include "lib/memory.h"
#include "lib/string.h"
#include "lib/stdio.h"

#include "drivers/ata/ata.h"

#include "fs/filesystem.h"

extern uint8_t fromUEFI;

FAT32Driver::FAT32Driver(Device* device)
	: device(device)
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

    temporaryBufferSize = BootSector->BytesPerSector * BootSector->SectorsPerCluster;
    temporaryBuffer = (uint8_t*)kmalloc(temporaryBufferSize);
    temporaryBuffer2 = (uint8_t*)kmalloc(temporaryBufferSize);

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

    kprintf("Statistics: \n");
    kprintf("\tFirst data sector: %d\n", FirstDataSector);
    kprintf("\tRoot directory start cluster: %d\n", RootDirStart);
    kprintf("\tMedia size in sectors: %d\n", TotalSectors);
    kprintf("\tNumber of clusters: %d\n\n", TotalClusters);
}

FAT32Driver::~FAT32Driver()
{
    kfree(temporaryBuffer);
    kfree(temporaryBuffer2);
}

int FAT32Driver::ReadFAT(uint32_t cluster)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return FAT32_ERROR_BAD_CLUSTER_VALUE;
	}

	uint32_t cluster_size = BootSector->SectorsPerCluster * BootSector->BytesPerSector;
	uint32_t fat_offset = cluster * 4;
	uint32_t fat_sector = BootSector->ReservedSectors + (fat_offset / cluster_size);
	uint32_t ent_offset = (fat_offset % cluster_size) / 4;
	 
	uint32_t fat_LBA = fat_sector + PartitionStart;
    device->Read((uint64_t)fat_LBA, (void*)temporaryBuffer2, 1);
	 
	uint32_t* FATtable = (uint32_t*)temporaryBuffer2;
	return FATtable[ent_offset] & 0x0FFFFFFF;
}

int FAT32Driver::WriteFAT(uint32_t cluster, uint32_t value)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return FAT32_ERROR_BAD_CLUSTER_VALUE;
	}

	uint32_t cluster_size = BootSector->SectorsPerCluster * BootSector->BytesPerSector;
	uint32_t fat_offset = cluster * 4;
	uint32_t fat_sector = BootSector->ReservedSectors + (fat_offset / cluster_size);
	uint32_t ent_offset = (fat_offset % cluster_size) / 4;

    uint32_t fat_LBA = fat_sector + PartitionStart;
    device->Read((uint64_t)fat_LBA, (void*)temporaryBuffer2, 1);
	 
	uint32_t* FATtable = (uint32_t*)temporaryBuffer2;
	FATtable[ent_offset] |= (value & 0x0FFFFFFF);

    device->Write((uint64_t)fat_LBA, (void*)temporaryBuffer2, 1);
	return 0;
}

uint32_t FAT32Driver::AllocateFreeFAT()
{
	uint32_t cluster = 2;
	uint32_t clusterStatus = FREE_CLUSTER;

	while (cluster < TotalClusters)
	{
		clusterStatus = ReadFAT(cluster);

		if (clusterStatus == FREE_CLUSTER)
		{
			if (WriteFAT(cluster, END_CLUSTER) == 0)
			{
				return cluster;
			}
			else
			{
				return BAD_CLUSTER;
			}
		}
		else if (clusterStatus < 0)
		{
			return BAD_CLUSTER;
		}

		cluster++;
	}

	return BAD_CLUSTER;
}

int FAT32Driver::ReadCluster(uint32_t cluster, void* buffer)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return FAT32_ERROR_BAD_CLUSTER_VALUE;
	}
	
	uint32_t start_sector = PartitionStart + (cluster - 2) * BootSector->SectorsPerCluster + FirstDataSector;
    device->Read(start_sector, (void*)buffer, 1);
	return 0;
}

int FAT32Driver::WriteCluster(uint32_t cluster, void* buffer)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return FAT32_ERROR_BAD_CLUSTER_VALUE;
	}

	uint32_t start_sector = PartitionStart + (cluster - 2) * BootSector->SectorsPerCluster + FirstDataSector;
    device->Write(start_sector, (void*)buffer, 1);
	return 0;
}

DirEntry* FAT32Driver::GetDirectories(uint32_t cluster, uint32_t attributes, bool exclusive, uint32_t* entryCount)
{
	DirEntry* ret = new DirEntry[MAX_ENTRIES_IN_DIRECTORY];

	if (cluster < 2 || cluster > TotalClusters)
	{
		return ret;
	}

	uint8_t default_hidden_attr = FILE_HIDDEN | FILE_SYSTEM;
	uint8_t attr_to_hide = default_hidden_attr;

	if (exclusive)
	{
		attr_to_hide = ~attributes;
	}
	else
	{
		attr_to_hide &= ~attributes;
	}

	ReadCluster(cluster, temporaryBuffer);

	DirectoryEntry* metadata = (DirectoryEntry*)temporaryBuffer;
	uint32_t meta_pointer_iterator = 0;
	*entryCount = 0;

	bool LFN = false;
	while (true)
	{
		if (metadata->name[0] == ENTRY_END)
		{
			break;
		}
		else if (strncmp(metadata->name, "..", 2) == 0 || strncmp(metadata->name, ".", 1) == 0)
		{
			if (metadata->name[1] == '.')
			{
				DirEntry entry = FromFATEntry(metadata, false);
				ret[*entryCount++] = entry;
			}
			else
			{
				DirEntry entry = FromFATEntry(metadata, false);
				ret[*entryCount++] = entry;
			}

			metadata++;
			meta_pointer_iterator++;
		}
		else if (((metadata->name)[0] == ENTRY_FREE) || ((metadata->attributes & FILE_LONG_NAME) == FILE_LONG_NAME) || ((metadata->attributes & attr_to_hide) != 0)) //if the entry is a free entry, or it contains an attribute not wanted
		{
			LFN = ((metadata->attributes & FILE_LONG_NAME) == FILE_LONG_NAME);

			//If we are under the cluster limit
			if (meta_pointer_iterator < BootSector->BytesPerSector * BootSector->SectorsPerCluster / sizeof(DirectoryEntry) - 1)
			{
				metadata++;
				meta_pointer_iterator++;
			}
			//Search next cluster
			else
			{
				int next_cluster = ReadFAT(cluster);
				if (next_cluster >= END_CLUSTER)
				{
					break;
				}
				else if (next_cluster < 0)
				{
					return ret;
				}
				else
				{
					//Search next cluster
					return GetDirectories(next_cluster, attributes, exclusive, entryCount);
				}
			}
		}
		else
		{
			DirEntry entry = FromFATEntry(metadata, LFN);
			ret[*entryCount++] = entry;

			LFN = false;

			metadata++;
			meta_pointer_iterator++;
		}
	}

	return ret;
}

int FAT32Driver::PrepareAddedDirectory(uint32_t cluster)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return FAT32_ERROR_BAD_CLUSTER_VALUE;
	}

	char* tempBuff = new char[BootSector->BytesPerSector * BootSector->SectorsPerCluster];
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
}

int FAT32Driver::DirectorySearch(const char* FilePart, uint32_t cluster, DirEntry* file, uint32_t* entryOffset)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return FAT32_ERROR_BAD_CLUSTER_VALUE;
	}

	bool searchingLFN = false;
	char searchName[13] = { 0 };
	memcpy(searchName, FilePart, 12);

	if (strlen((char*)FilePart) > 12)
	{
		searchingLFN = true;
	}

	if ((IsFATFormat(searchName) != 0) && !searchingLFN)
	{
		ConvertToFATFormat(searchName);
	}

	ReadCluster(cluster, temporaryBuffer);

	DirectoryEntry* metadata = (DirectoryEntry*)temporaryBuffer;
	uint32_t meta_pointer_iterator = 0;

	while (1)
	{
		if (metadata->name[0] == ENTRY_END)
		{
			break;
		}
		else if(((metadata->attributes & FILE_LONG_NAME) == FILE_LONG_NAME) || !(Compare(metadata, FilePart, searchingLFN)))
		{
			//If we are under the cluster limit
			if (meta_pointer_iterator < BootSector->BytesPerSector * BootSector->SectorsPerCluster / sizeof(DirectoryEntry) - 1)
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
				else if (next_cluster < 0)
				{
					return FAT32_ERROR_BAD_CLUSTER_VALUE;
				}
				else
				{
					//Search next cluster
					return DirectorySearch(FilePart, next_cluster, file, entryOffset);
				}
			}
		}
		else
		{
			if (file != nullptr)
			{
				strcpy(file->name, FilePart);
				file->cluster = metadata->clusterLow | (metadata->clusterHigh);
				file->attributes = metadata->attributes;
				file->size = metadata->fileSize;
			}

			if (entryOffset != nullptr)
			{
				*entryOffset = meta_pointer_iterator;
			}

			return 0;
		}
	}

	return FAT32_ERROR_FILE_NOT_FOUND;
}

int FAT32Driver::DirectoryAdd(uint32_t cluster, DirEntry file)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return FAT32_ERROR_BAD_CLUSTER_VALUE;
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
		if (((metadata->name[0] != ENTRY_FREE) && (metadata->name[0] != ENTRY_END)))
		{
			if (meta_pointer_iterator < BootSector->BytesPerSector * BootSector->SectorsPerCluster / sizeof(DirectoryEntry) - 1)
			{
				metadata++;
				meta_pointer_iterator++;
			}
			else
			{
				uint32_t next_cluster = ReadFAT(cluster);

				if (next_cluster >= END_CLUSTER)
				{
					next_cluster = AllocateFreeFAT();

					if (next_cluster == BAD_CLUSTER)
					{
						return FAT32_ERROR_BAD_CLUSTER_VALUE;
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

				if (meta_pointer_iterator < BootSector->BytesPerSector * BootSector->SectorsPerCluster / sizeof(DirectoryEntry) - 1)
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
						next_cluster = AllocateFreeFAT();

						if (next_cluster == BAD_CLUSTER)
						{
							return FAT32_ERROR_BAD_CLUSTER_VALUE;
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
			uint32_t new_cluster = AllocateFreeFAT();

			if (new_cluster == BAD_CLUSTER)
			{
				return FAT32_ERROR_BAD_CLUSTER_VALUE;
			}

			char buffer[512];
			memset(buffer, 0, 512);
			WriteCluster(new_cluster, buffer);

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

	return FAT32_ERROR_NO_FREE_SPACE;
}

int FAT32Driver::OpenFile(const char* filePath, DirEntry* fileMeta)
{
	if (fileMeta == nullptr)
	{
		return FAT32_ERROR_INVALID_ARGUMENTS;
	}

	char fileNamePart[256];
	uint16_t start = 2; //Skip the "~/" part
	uint32_t active_cluster = RootDirStart;

	DirEntry fileInfo;

	uint32_t iterator = 2;
	for (iterator = 2; filePath[iterator - 1] != 0; iterator++)
	{
		if (filePath[iterator] == '/' || filePath[iterator] == 0)
		{
			memset(fileNamePart, 0, 256);
			memcpy(fileNamePart, (void*)((uint64_t)filePath + start), iterator - start);

			int retVal = DirectorySearch(fileNamePart, active_cluster, &fileInfo, nullptr);

			if (retVal != 0)
			{
				return retVal;
			}

			start = iterator + 1;
			active_cluster = fileInfo.cluster;
		}
	}

	*fileMeta = fileInfo;
	return 0;
}

int FAT32Driver::CreateFile(const char* filePath, DirEntry* fileMeta)
{
	char fileNamePart[256];

	uint16_t start = 2;
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

				int retVal = DirectorySearch(fileNamePart, active_cluster, &fileInfo, nullptr);

				if (retVal != 0)
				{
					return retVal;
				}

				start = iterator + 1;
				active_cluster = fileInfo.cluster;
			}
		}
	}

	//Makes sure there's no other file like this
	int retVal = DirectorySearch(fileMeta->name, active_cluster, nullptr, nullptr);
	if (retVal != FAT32_ERROR_FILE_NOT_FOUND)
	{
		return retVal;
	}

	if ((fileInfo.attributes & FILE_DIRECTORY) != FILE_DIRECTORY)
	{
		return FAT32_ERROR_NOT_DIRECTORY;
	}

	DirectoryAdd(active_cluster, *fileMeta);
	return DirectorySearch(fileMeta->name, active_cluster, (DirEntry*)&fileInfo, nullptr);
}

int FAT32Driver::ReadFile(DirEntry fileMeta, uint64_t offset, void* buffer, uint64_t bytes)
{
	if ((fileMeta.attributes & FILE_DIRECTORY) == FILE_DIRECTORY)
	{
		return FAT32_ERROR_DIRECTORY;
	}

	if ((bytes + offset) > fileMeta.size)
	{
		bytes = fileMeta.size - offset;
	}

	uint32_t cluster = fileMeta.cluster;

	uint8_t* buff = (uint8_t*)buffer;
	uint64_t bytes_so_far = 0;
	while (bytes_so_far < (bytes + offset))
	{
		if (bytes_so_far < offset)
		{
			bytes_so_far += BootSector->SectorsPerCluster * BootSector->BytesPerSector;
			continue;
		}

		uint32_t toRead = (bytes + offset) - bytes_so_far;
		if (toRead > BootSector->SectorsPerCluster * BootSector->BytesPerSector)
		{
			toRead = BootSector->SectorsPerCluster * BootSector->BytesPerSector;
		}

		ReadCluster(cluster, buff);
		buff += BootSector->SectorsPerCluster * BootSector->BytesPerSector;
		bytes_so_far += toRead;

		cluster = ReadFAT(cluster);
		if (cluster == BAD_CLUSTER)
		{
			return FAT32_ERROR_BAD_CLUSTER_VALUE;
		}
	}
	
	return 0;
}

int FAT32Driver::WriteFile(DirEntry fileMeta, uint64_t offset, void* buffer, uint64_t bytes)
{
	uint32_t active_cluster = fileMeta.cluster;
	uint64_t dataLeft = bytes;
	uint64_t bytes_so_far = 0;
	while (dataLeft > 0)
	{
		if (bytes_so_far < offset)
		{
			bytes_so_far += BootSector->BytesPerSector * BootSector->SectorsPerCluster;
			continue;
		}

		uint32_t dataWrite = 0;
		if (dataLeft >= ((uint32_t)BootSector->BytesPerSector * (uint32_t)BootSector->SectorsPerCluster))
		{
			dataWrite = BootSector->BytesPerSector * BootSector->SectorsPerCluster;
		}
		else
		{
			dataWrite = dataLeft;
		}

		WriteCluster(active_cluster, (void*)((uint64_t)buffer + fileMeta.size - dataLeft));
		dataLeft -= dataWrite;

		if (dataLeft == 0)
		{
			break;
		}

		uint32_t new_cluster = AllocateFreeFAT();
		if (new_cluster == BAD_CLUSTER)
		{
			return FAT32_ERROR_BAD_CLUSTER_VALUE;
		}

		WriteFAT(active_cluster, new_cluster);
		active_cluster = new_cluster;
	}

	return 0;
}

int FAT32Driver::IsFATFormat(char* name)
{
	short retVal = 0;

	if ((strncmp(name, ".          ", 11) == 0) || (strncmp(name, "..         ", 11) == 0))
	{
		return 0;
	}

	if (strlen(name) != 11)
	{
		return FAT32_ERROR_NOT_FAT_NAME;
	}

	unsigned short iterator;
	for (iterator = 0; iterator < 11; iterator++)
	{
		if (name[iterator] < 0x20 && name[iterator] != 0x05)
		{
			retVal = FAT32_ERROR_NOT_FAT_NAME;
		}

		switch (name[iterator])
		{
		/*case 0x2E:
		{
			if ((retVal & 8) == 8) //a previous dot has already triggered this case
				retVal |= 4;

			retVal ^= 2; //remove NOT_CONVERTED_YET flag if already set

			break;
		}*/
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
			retVal = FAT32_ERROR_NOT_FAT_NAME;
		}

		if (name[iterator] >= 'a' && name[iterator] <= 'z')
		{
			retVal = FAT32_ERROR_NOT_FAT_NAME;
		}
	}

	return retVal;
}

char* FAT32Driver::ConvertToFATFormat(char* input)
{
	uint32_t counter = 0;

	for (uint32_t i = 0; i < 12; i++)
	{
		if(input[i] > 'a' && input[i] < 'z')
		{
			input[i] += ('A' - 'a');
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
		longEntries = long_entries;
		LongDirectoryEntry* entries = new LongDirectoryEntry[long_entries + 1];
		LongDirectoryEntry* start = entries + (long_entries - 1);

		char shortName[11];
		for (uint32_t i = 0; i < 6; i++)
		{
			if(namePtr[i] > 'a' && namePtr[i] < 'z')
			{
				shortName[i] = namePtr[i] + ('A' - 'a');
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
					if(namePtr[i + 1 + j] > 'a' && namePtr[i + 1 + j] < 'z')
					{
						shortName[i] = namePtr[i + 1 + j] + ('A' - 'a');
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

			start->order = j + 1;
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