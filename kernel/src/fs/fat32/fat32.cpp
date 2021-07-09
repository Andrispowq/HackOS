#include "fat32.h"

#include "lib/memory.h"
#include "lib/string.h"
#include "lib/stdio.h"

uint64_t FAT32_ActiveFile::GetSize() const
{
	return entry.size;
}

const char* FAT32_ActiveFile::GetName() const
{
	return entry.name;
}

FAT32::FAT32(Device* device)
	: Filesystem(device)
{
	driver = new FAT32Driver(device);
}

FAT32::~FAT32()
{
	delete driver;
}

ActiveFile* FAT32::OpenFile(const char* path)
{
	DirEntry entry;
	int ret = driver->OpenFile(path, &entry);

	if (ret != 0)
	{
		return nullptr;
	}

	char* ptr = new char[strlen((char*)path) + 1];
	strcpy(ptr, (char*)path);

	FAT32_ActiveFile* file = new FAT32_ActiveFile();
	file->path = ptr;
	file->entry = entry;

	return (ActiveFile*)file;
}

void FAT32::CloseFile(ActiveFile* file)
{
	delete[] ((FAT32_ActiveFile*)file)->path;
	delete file;
}

ActiveFile* FAT32::CreateFile(const char* path, const char* name, permissions_t permissions)
{
	uint32_t size = 512;
	uint8_t attributes = 0;
	attributes |= ((permissions & READ_ONLY) == READ_ONLY) << 0;
	attributes |= ((permissions & HIDDEN) == HIDDEN) << 1;
	attributes |= ((permissions & SYSTEM) == SYSTEM) << 2;
	attributes |= ((permissions & VOLUME_ID) == VOLUME_ID) << 3;
	attributes |= ((permissions & DIRECTORY) == DIRECTORY) << 4;
	attributes |= ((permissions & ARCHIVE) == ARCHIVE) << 5;

	DirEntry entry;
	strcpy(entry.name, name);
	entry.cluster = 0;
	entry.attributes = attributes;
	entry.size = size;
	driver->CreateFile(path, &entry);

	size_t pathLen = strlen((char*)path);
	size_t fnameLen = strlen((char*)name);
	size_t total = pathLen + fnameLen + 2;
	char* ptr = new char[total];
	memcpy(ptr, path, pathLen);
	ptr[pathLen] = '/';
	memcpy(&ptr[pathLen + 1], name, fnameLen);
	ptr[pathLen + 1 + fnameLen] = 0;

	FAT32_ActiveFile* file = new FAT32_ActiveFile();
	file->path = ptr;
	file->entry = entry;

	return (ActiveFile*)file;
}

void FAT32::ResizeFile(ActiveFile* file, uint64_t new_size)
{
	driver->ResizeFile(((FAT32_ActiveFile*)file)->entry, new_size);
	((FAT32_ActiveFile*)file)->entry.size = new_size;
	file->seek_position = 0;
}

void FAT32::DeleteFile(ActiveFile* file)
{
	driver->DeleteFile(((FAT32_ActiveFile*)file)->entry);
	CloseFile(file);
}

uint64_t FAT32::GetDirectoryEntryCount(const char* path)
{
	DirEntry entry;
	driver->OpenFile(path, &entry);

	vector<DirEntry> entries = driver->GetDirectories(entry.cluster, 0, false);
	
	if((strcmp((char*)path, (char*)"~") == 0) || (strcmp((char*)path, (char*)"~/") == 0))
	{
		return entries.size();
	}
	else
	{
		return entries.size() - 2; //we don't count the '.' and '..' entries
	}
}

ActiveFile* FAT32::GetFile(const char* path, uint64_t index)
{
	DirEntry entry;
	driver->OpenFile(path, &entry);

	vector<DirEntry> entries = driver->GetDirectories(entry.cluster, 0, false);

	if(strcmp((char*)path, (char*)"~") != 0)
	{
		index += 2;
	}

	if(index < entries.size())
	{
		DirEntry file = entries[index];

		size_t pathlen = strlen((char*)path);
		size_t namelen = strlen(file.name);
		char* totalPath = new char[pathlen + namelen + 2];
		memcpy(totalPath, path, pathlen);
		totalPath[pathlen] = '/';
		memcpy(&totalPath[pathlen + 1], file.name, namelen);
		totalPath[pathlen + 1 + namelen] = 0;

		ActiveFile* opened = OpenFile(totalPath);
		delete[] totalPath;
		return opened;
	}

	return nullptr;
}

int FAT32::Read(ActiveFile* file, void* buffer, uint64_t nBytes)
{
	uint64_t seek_pos = file->seek_position;
	driver->ReadFile(((FAT32_ActiveFile*)file)->entry, seek_pos, buffer, nBytes);
	file->seek_position += nBytes;

	return 0;
}

int FAT32::Write(ActiveFile* file, void* buffer, uint64_t nBytes)
{
	uint64_t seek_pos = file->seek_position;
	driver->WriteFile(((FAT32_ActiveFile*)file)->entry, seek_pos, buffer, nBytes);
	file->seek_position += nBytes;

	if (((FAT32_ActiveFile*)file)->entry.size < file->seek_position)
	{
		((FAT32_ActiveFile*)file)->entry.size = (uint32_t)file->seek_position;
	}

	return 0;
}