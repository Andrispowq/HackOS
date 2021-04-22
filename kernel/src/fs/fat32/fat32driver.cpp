#include "fat32driver.h"

#include "lib/memory.h"
#include "lib/string.h"
#include "lib/stdio.h"

#include "drivers/ata/ata.h"

FAT32Driver::FAT32Driver(Device* device)
	: device(device)
{
    //The MBR is still at 0x0600, extract the boot partition's start LBA
    uint32_t* MBR = (uint32_t*)0x0600;

	//If we don't find any partition tables valid, we should assume that we aren't partitioned
	PartitionStart = 0;

    uint64_t BaseOffset = 446;
    for(uint64_t i = 0; i < 4; i++)
    {
        uint32_t* ptr = (uint32_t*)((uint64_t)MBR + BaseOffset + i * 16);
        //The 8th byte holds a 4-byte long LBA entry
        //The first byte must be 0x80 if it is bootable
        uint32_t LBA = ptr[2];
        uint8_t active = *(uint8_t*)ptr;

        if(active == 0x80)
        {
            PartitionStart = LBA;
			break;
        }
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

uint32_t FAT32Driver::ReadFAT(uint32_t cluster)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
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

uint32_t FAT32Driver::WriteFAT(uint32_t cluster, uint32_t value)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}

	uint32_t cluster_size = BootSector->SectorsPerCluster * BootSector->BytesPerSector;
	uint32_t fat_offset = cluster * 4;
	uint32_t fat_sector = BootSector->ReservedSectors + (fat_offset / cluster_size);
	uint32_t ent_offset = (fat_offset % cluster_size) / 4;

    uint32_t fat_LBA = fat_sector + PartitionStart;
    device->Read((uint64_t)fat_LBA, (void*)temporaryBuffer2, 1);
	 
	uint32_t* FATtable = (uint32_t*)temporaryBuffer2;
	FATtable[ent_offset] = value;

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

uint32_t FAT32Driver::ReadCluster(uint32_t cluster, void* buffer)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}
	
	uint32_t start_sector = PartitionStart + (cluster - 2) * BootSector->SectorsPerCluster + FirstDataSector;
    device->Read(start_sector, (void*)buffer, 1);
	return 0;
}

uint32_t FAT32Driver::WriteCluster(uint32_t cluster, void* buffer)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}

	uint32_t start_sector = PartitionStart + (cluster - 2) * BootSector->SectorsPerCluster + FirstDataSector;
    device->Write(start_sector, (void*)buffer, 1);
	return 0;
}

//The entryCount should be zero on the first invocation, to ensure correct count
DirectoryEntry* FAT32Driver::ListDirectory(uint32_t cluster, uint32_t attributes, bool exclusive, uint32_t* entryCount)
{
    //For now, allocate 20 direntries
	DirectoryEntry* ret = (DirectoryEntry*)kmalloc(sizeof(DirectoryEntry) * 20);
    memset(ret, 0, sizeof(DirectoryEntry) * 20);

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

	while (true)
	{
		if (metadata->name[0] == ENTRY_END)
		{
			break;
		}
		else if (memcmp(metadata->name, (void*)"..", 2) == 0 || memcmp(metadata->name, (void*)".", 1) == 0)
		{
			if (metadata->name[1] == '.')
			{
				DirectoryEntry entry;
				memcpy(&entry, metadata, sizeof(DirectoryEntry));
				ret[*entryCount++] = entry;
			}
			else
			{
				DirectoryEntry entry;
				memcpy(&entry, metadata, sizeof(DirectoryEntry));
				ret[*entryCount++] = entry;
			}

			metadata++;
			meta_pointer_iterator++;
		}
		else if (((metadata->name)[0] == ENTRY_FREE) || ((metadata->attributes & FILE_LONG_NAME) == FILE_LONG_NAME) || ((metadata->attributes & attr_to_hide) != 0)) //if the entry is a free entry, a long name, or it contains an attribute not wanted
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
					return ret;
				}
				else
				{
					//Search next cluster
					return ListDirectory(next_cluster, attributes, exclusive, entryCount);
				}
			}
		}
		else
		{
			DirectoryEntry entry;
			memcpy(&entry, metadata, sizeof(DirectoryEntry));
			ret[*entryCount++] = entry;

			metadata++;
			meta_pointer_iterator++;
		}
	}

	return ret;
}

int FAT32Driver::DirectorySearch(const char* FilePart, uint32_t cluster, DirectoryEntry* file, uint32_t* entryOffset)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}

	char searchName[13] = { 0 };
	strcpy(searchName, FilePart);

	if (IsFATFormat(searchName) != 0)
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
		else if (memcmp((char*)metadata->name, searchName, 11) != 0)
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
					return -1;
				}
				else
				{
					//Search next cluster
					return DirectorySearch(FilePart, next_cluster, file, entryOffset);
				}
			}
		}
		//found a match
		else
		{
			if (file != nullptr)
			{
				memcpy(file, metadata, sizeof(DirectoryEntry));
			}

			if (entryOffset != nullptr)
			{
				*entryOffset = meta_pointer_iterator;
			}

			return 0;
		}
	}

	return -2;
}

int FAT32Driver::DirectoryAdd(uint32_t cluster, DirectoryEntry* file, void* writeBuffer)
{
	if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}

	if (IsFATFormat(file->name) != 0)
	{
		return -1;
	}

	ReadCluster(cluster, temporaryBuffer);

	DirectoryEntry* metadata = (DirectoryEntry*)temporaryBuffer;
	uint32_t meta_pointer_iterator = 0;

	while (1)
	{
		if (metadata->name[0] != ENTRY_FREE && metadata->name[0] != ENTRY_END)
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
						return -1;
					}

					WriteFAT(cluster, next_cluster);
				}

				return DirectoryAdd(next_cluster, file, writeBuffer);
			}
		}
		else
		{
			uint16_t dot_checker = 0;
			for (dot_checker = 0; dot_checker < 11; dot_checker++)
			{
				if (file->name[dot_checker] == '.')
				{
					return -1;
				}
			}

			file->ctime_date = 0;
			file->ctime_time = 0;
			file->ctime_ms = 0;
			file->atime_date = 0;
			file->mtime_date = 0;
			file->mtime_time = 0;

			uint32_t new_cluster = AllocateFreeFAT();

			if (new_cluster == BAD_CLUSTER)
			{
				return -1;
			}

			file->clusterLow = new_cluster & 0xFFFF;
			file->clusterHigh = (new_cluster >> 16) & 0xFFFF;

			memcpy(metadata, file, sizeof(DirectoryEntry));
			WriteCluster(cluster, temporaryBuffer); //Write the modified stuff back

			WriteCluster(new_cluster, writeBuffer);
			return 0;
		}
	}
	return -1;
}

int FAT32Driver::GetFile(const char* filePath, void** fileContents, DirectoryEntry* fileMeta)
{
	if (fileContents == nullptr || fileMeta == nullptr)
	{
		return -1;
	}

	char fileNamePart[256];
	uint16_t start = 3; //Skip the "C:\" part
	uint32_t active_cluster = RootDirStart;

	DirectoryEntry fileInfo;

	uint32_t iterator = 3;
	for (iterator = 3; filePath[iterator - 1] != 0; iterator++)
	{
		if (filePath[iterator] == '\\' || filePath[iterator] == 0)
		{
			memset(fileNamePart, 0, 256);
			memcpy(fileNamePart, (void*)((uint64_t)filePath + start), iterator - start);

			int retVal = DirectorySearch(fileNamePart, active_cluster, &fileInfo, nullptr);

			if (retVal != 0)
			{
				return retVal;
			}

			start = iterator + 1;
			active_cluster = fileInfo.clusterLow | (fileInfo.clusterHigh << 16);
		}
	}

	*fileMeta = fileInfo;

	if ((fileInfo.attributes & FILE_DIRECTORY) != FILE_DIRECTORY)
	{
		uint32_t cluster = fileInfo.clusterLow | (fileInfo.clusterHigh);
		uint32_t clusterReadCount = 0;
		uint8_t* buffer = new uint8_t[fileInfo.fileSize];
		uint8_t* buff = buffer;
		while (cluster < END_CLUSTER)
		{
			ReadCluster(cluster, buff);
			buff += BootSector->SectorsPerCluster * BootSector->BytesPerSector;
			clusterReadCount++;
			cluster = ReadFAT(cluster);
			if (cluster == BAD_CLUSTER)
			{
				return -1;
			}
		}

		*fileContents = (void*)buffer;

		return 0;
	}
	else
	{
		return -3;
	}
}

int FAT32Driver::PutFile(const char* filePath, void* fileContents, DirectoryEntry* fileMeta)
{
	if (IsFATFormat(fileMeta->name) != 0)
	{
		kprintf("Invalid file name!\n");
	}

	char fileNamePart[256];

	uint16_t start = 3;
	uint32_t active_cluster = RootDirStart;
	DirectoryEntry fileInfo;

	uint32_t iterator = 3;
	if (strcmp((char*)filePath, "C:\\") == 0)
	{
		fileInfo.attributes = FILE_DIRECTORY | FILE_VOLUME_ID;
		fileInfo.fileSize = 0;
		fileInfo.clusterHigh = (active_cluster >> 16) & 0xFFFF;
		fileInfo.clusterLow = active_cluster & 0xFFFF;
	}
	else
	{
		for (iterator = 3; filePath[iterator - 1] != 0; iterator++)
		{
			if (filePath[iterator] == '\\' || filePath[iterator] == 0)
			{
				memset(fileNamePart, 0, 256);
				memcpy(fileNamePart, (void*)((uint64_t)filePath + start), iterator - start);

				int retVal = DirectorySearch(fileNamePart, active_cluster, &fileInfo, nullptr);

				if (retVal != 0)
				{
					return retVal;
				}

				start = iterator + 1;
				active_cluster = fileInfo.clusterLow | (fileInfo.clusterHigh << 16);
			}
		}
	}

	char output[13];
	ConvertFromFATFormat((char*)fileMeta->name, output);

	//Makes sure there's no other file like this
	int retVal = DirectorySearch(output, active_cluster, nullptr, nullptr);
	if (retVal != -2)
	{
		return retVal;
	}

	if ((fileInfo.attributes & FILE_DIRECTORY) == FILE_DIRECTORY)
	{
		uint32_t buff[512];
		DirectoryAdd(active_cluster, fileMeta, buff);

		char output[13];
		ConvertFromFATFormat((char*)fileMeta->name, output);

		retVal = DirectorySearch(output, active_cluster, &fileInfo, nullptr);

		if (retVal != 0)
		{
			return retVal;
		}

		active_cluster = fileInfo.clusterLow | (fileInfo.clusterHigh << 16);
		uint32_t dataLeft = fileMeta->fileSize;

		while (dataLeft > 0)
		{
			uint32_t dataWrite = 0;
			if (dataLeft >= BootSector->BytesPerSector * BootSector->SectorsPerCluster)
			{
				dataWrite = BootSector->BytesPerSector * BootSector->SectorsPerCluster;
			}
			else
			{
				dataWrite = dataLeft;
			}

			WriteCluster(active_cluster, (void*)((uint64_t)fileContents + fileMeta->fileSize - dataLeft));
			dataLeft -= dataWrite;

			if (dataLeft == 0)
			{
				break;
			}

			uint32_t new_cluster = AllocateFreeFAT();
			if (new_cluster == BAD_CLUSTER)
			{
				return -1;
			}

			WriteFAT(active_cluster, new_cluster);
			active_cluster = new_cluster;
		}

		return 0;
	}
	else
	{
		return -2;
	}
}

int FAT32Driver::IsFATFormat(char* name)
{
	short retVal = 0;

	unsigned short iterator;
	for (iterator = 0; iterator < 11; iterator++)
	{
		if (name[iterator] < 0x20 && name[iterator] != 0x05)
		{
			retVal = retVal | BAD_CHARACTER;
		}

		switch (name[iterator])
		{
		case 0x2E:
		{
			if ((retVal & NOT_CONVERTED_YET) == NOT_CONVERTED_YET) //a previous dot has already triggered this case
				retVal |= TOO_MANY_DOTS;

			retVal ^= NOT_CONVERTED_YET; //remove NOT_CONVERTED_YET flag if already set

			break;
		}
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
			retVal = retVal | BAD_CHARACTER;
		}

		if (name[iterator] >= 'a' && name[iterator] <= 'z')
		{
			retVal = retVal | LOWERCASE_ISSUE;
		}
	}

	return retVal;
}

char* FAT32Driver::ConvertToFATFormat(char* input)
{
	uint32_t counter = 0;

	char searchName[13] = { '\0' };
	uint16_t dotPos = 0;

	counter = 0;
	while (counter <= 8) //copy all the characters from filepart into searchname until a dot or null character is encountered
	{
		if (input[counter] == '.' || input[counter] == '\0')
		{
			dotPos = counter;
			counter++; //iterate off dot
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
		if (input[counter] != '\0')
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