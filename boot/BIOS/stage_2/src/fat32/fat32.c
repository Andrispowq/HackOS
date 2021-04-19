#include "fat32.h"
#include "libc/memory.h"
#include "libc/string.h"

static FAT32BootSector* BootSector;

static uint32_t VolumeStartSector;

static uint32_t FirstDataSector;
static uint32_t RootDirStart;
static uint32_t TotalSectors;
static uint32_t TotalClusters;

//temporary buffers for read operations
static uint32_t* temporaryBuffer;
static uint32_t* temporaryBuffer2;

void InitialiseFAT()
{
    //The bootsector is loaded at 0x7C00, let's place our bootsector struct at that address
    BootSector = (FAT32BootSector*)0x7C00;

    //The MBR is still at 0x0600, extract the boot partition's start LBA
    uint32_t* MBR = (uint32_t*)0x0600;

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
            VolumeStartSector = LBA;
			break;
        }
    }

    temporaryBuffer = (uint32_t*)kmalloc(BootSector->BytesPerSector * BootSector->SectorsPerCluster);
    temporaryBuffer2 = (uint32_t*)kmalloc(BootSector->BytesPerSector * BootSector->SectorsPerCluster);

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
}

int ReadFAT(uint32_t cluster)
{
    if(cluster < 2 || cluster > TotalClusters)
    {
        return -1;
    }

	uint32_t cluster_size = BootSector->SectorsPerCluster * BootSector->BytesPerSector;
	uint32_t fat_offset = cluster * 4;
	uint32_t fat_sector = BootSector->ReservedSectors + (fat_offset / cluster_size);
	uint32_t ent_offset = (fat_offset % cluster_size) / 4;

    uint32_t fat_LBA = fat_sector + VolumeStartSector;
    ReadSectorsATA((uint64_t)temporaryBuffer2, fat_LBA, 1);
	 
	uint32_t* FATtable = (uint32_t*)temporaryBuffer2;
	return FATtable[ent_offset] & 0x0FFFFFFF;
}

int ReadCluster(uint32_t cluster, void* buffer)
{
    if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}
	
	uint32_t start_sector = VolumeStartSector + (cluster - 2) * BootSector->SectorsPerCluster + FirstDataSector;
    ReadSectorsATA((uint64_t)buffer, start_sector, 1);
    return 0;
}

int DirectorySearch(const char* filePart, uint32_t cluster, DirectoryEntry* file)
{
    if (cluster < 2 || cluster > TotalClusters)
	{
		return -1;
	}

	char searchName[13] = { 0 };
	strcpy(searchName, filePart);

	if (IsFATFormat(searchName) != 0)
	{
		ConvertToFATFormat(searchName);
	}

	ReadCluster(cluster, temporaryBuffer);

	DirectoryEntry* metadata = (DirectoryEntry*)temporaryBuffer;
	uint32_t meta_pointer_iterator = 0;

    while (1)
	{
		if (metadata->name == ENTRY_END)
		{
			break;
		}
		else if (memcmp((void*)metadata->name, (void*)searchName, 11) != 0)
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
					return DirectorySearch(filePart, next_cluster, file);
				}
			}
		}
		//found a match
		else
		{
			if (file != 0)
			{
				memcpy(file, metadata, sizeof(DirectoryEntry));
			}

			return 0;
		}
	}

    return -2;
}

int GetFile(const char* path, void** fileContents, DirectoryEntry* fileMeta)
{
    if (fileContents == 0 || fileMeta == 0)
	{
		return -1;
	}

	char fileNamePart[256];
	uint16_t start = 3; //Skip the "C:\" part
	uint32_t active_cluster = RootDirStart;

	DirectoryEntry fileInfo;

	uint32_t iterator = 3;
	for (iterator = 3; path[iterator - 1] != 0; iterator++)
	{
		if (path[iterator] == '\\' || path[iterator] == 0)
		{
			memset(fileNamePart, 0, 256);
			memcpy(fileNamePart, (void*)((uint64_t)path + start), iterator - start);

			int retVal = DirectorySearch(fileNamePart, active_cluster, &fileInfo);

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
		uint8_t* buffer = (uint8_t*)kmalloc(fileInfo.fileSize);
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

int IsFATFormat(char* name)
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

void ConvertFromFATFormat(char* input, char* output)
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

char* ConvertToFATFormat(char* input)
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
