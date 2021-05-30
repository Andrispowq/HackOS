#include "fat32.h"

#include "lib/memory.h"
#include "lib/string.h"
#include "lib/stdio.h"

FAT32_FolderStructure::~FAT32_FolderStructure()
{
	for(size_t i = 0; i < children.size(); i++)
	{
		delete children[i];
	}
}

FAT32::FAT32(Device* device)
	: Filesystem(device)
{
	driver = new FAT32Driver(device);
	uint32_t rootDirStart = driver->GetRootDirStart();

	root = new FAT32_FolderStructure();
	root->parent = nullptr;
	strcpy(root->entry.name, "~");
	root->entry.size = 0;
	root->entry.attributes = 0;
	root->entry.cluster = rootDirStart;

	current = root;

	vector<uint32_t> clustersToRead;
	clustersToRead.push_back(rootDirStart);
	vector<uint32_t> nextClustersToRead;

	while (clustersToRead.size() != 0)
	{
		for (size_t c = 0; c < clustersToRead.size(); c++)
		{
			vector<DirEntry> rootDir = driver->GetDirectories(clustersToRead[c], 0, false);

			for (size_t i = 0; i < rootDir.size(); i++)
			{
				DirEntry parent = rootDir[0];
				DirEntry entry = rootDir[i];

				if (((entry.attributes & FILE_DIRECTORY) == FILE_DIRECTORY) && (entry.name[0] != '.'))
				{
					nextClustersToRead.push_back(entry.cluster);
				}

				FAT32_FolderStructure* curr = new FAT32_FolderStructure();
				FAT32_FolderStructure* par_dir = GetFolderByCluster(parent.cluster);

				//If we don't have '.' and '..' entries, we're in the root directory
				if (parent.name[0] != '.')
				{
					curr->parent = root;
					curr->entry = entry;

					root->children.push_back(curr);
				}
				else
				{
					curr->parent = par_dir;
					curr->entry = entry;

					par_dir->children.push_back(curr);
				}
			}
		}

		clustersToRead = nextClustersToRead;
		nextClustersToRead.clear();
	}
}

FAT32::~FAT32()
{
	delete root;
	delete driver;
}

FAT32_ActiveFile* FAT32::OpenFile(const char* path)
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
	file->file = RetrieveFromRoot(entry);
	file->path = ptr;

	if (file->file == nullptr)
	{
		delete[] file->path;
		delete file;
		return nullptr;
	}

	return file;
}

FAT32_ActiveFile* FAT32::CreateFile(const char* path, const char* fileName, uint8_t attributes, uint64_t size)
{
	DirEntry entry;
	strcpy(entry.name, fileName);
	entry.cluster = 0;
	entry.attributes = attributes;
	entry.size = (uint32_t)size;
	driver->CreateFile(path, &entry);

	AddToRecords(path, entry); //For now, we add it to the current directory

	size_t pathLen = strlen((char*)path);
	size_t fnameLen = strlen((char*)fileName);
	size_t total = pathLen + fnameLen + 2;
	char* ptr = new char[total];
	memcpy(ptr, path, pathLen);
	ptr[pathLen] = '/';
	memcpy(&ptr[pathLen + 1], fileName, fnameLen);
	ptr[pathLen + 1 + fnameLen] = 0;

	FAT32_ActiveFile* file = new FAT32_ActiveFile();
	file->file = RetrieveFromRoot(entry);
	file->path = ptr;

	return file;
}

void FAT32::DeleteFile(FAT32_ActiveFile* file)
{
	driver->DeleteFile(file->file->entry);

	DeleteFromRecords(file->path);
	CloseFile(file);
}

void FAT32::CloseFile(FAT32_ActiveFile* file)
{
	delete[] file->path;
	delete file;
}

int FAT32::ReadFile(FAT32_ActiveFile* file, void* buffer, uint64_t nBytes)
{
	uint64_t seek_pos = file->seek_position;
	driver->ReadFile(file->file->entry, seek_pos, buffer, nBytes);
	file->seek_position += nBytes;

	return 0;
}

int FAT32::WriteFile(FAT32_ActiveFile* file, void* buffer, uint64_t nBytes)
{
	uint64_t seek_pos = file->seek_position;
	driver->WriteFile(file->file->entry, seek_pos, buffer, nBytes);
	file->seek_position += nBytes;

	if (file->file->entry.size < file->seek_position)
	{
		file->file->entry.size = (uint32_t)file->seek_position;
	}

	return 0;
}

int FAT32::ResizeFile(FAT32_ActiveFile* file, uint64_t new_size)
{
	driver->ResizeFile(file->file->entry, new_size);
	file->file->entry.size = new_size;
	file->seek_position = 0;

	return 0;
}

void FAT32::AddToRecords(const char* path, DirEntry entry)
{
	FAT32_FolderStructure* parent = root;

	if (path != "~")
	{
		uint32_t last = 2;
		size_t pathLen = strlen((char*)path);
		for (uint32_t i = 2; i < pathLen; i++)
		{
			if (path[i] == '/' || (i == (pathLen - 1)))
			{
				size_t size = i - last;
				if (i == (pathLen - 1))
				{
					size++;
				}

				char* sub = new char[size - last + 1];
				memcpy(sub, &path[last], size);
				sub[size] = 0;
				last = i + 1;

				for (uint32_t j = 0; j < parent->children.size(); j++)
				{
					auto elem = parent->children[j];
					if (strcpy(sub, elem->entry.name) == 0)
					{
						parent = elem;
						break;
					}
				}

				delete[] sub;
			}
		}
	}

	FAT32_FolderStructure* curr = new FAT32_FolderStructure();

	curr->parent = parent;
	curr->entry = entry;

	parent->children.push_back(curr);
}

void FAT32::DeleteFromRecords(const char* name)
{
	FAT32_FolderStructure* curr = root;
	size_t index = 0;

	if (name != "~")
	{
		uint32_t last = 2;
		size_t pathLen = strlen((char*)name);
		for (uint32_t i = 2; i < pathLen; i++)
		{
			if (name[i] == '/' || (i == (pathLen - 1)))
			{
				size_t size = i - last;
				if (i == (pathLen - 1))
				{
					size++;
				}

				char* sub = new char[size - last + 1];
				memcpy(sub, &name[last], size);
				sub[size] = 0;
				last = i + 1;

				for (uint32_t j = 0; j < curr->children.size(); j++)
				{
					auto elem = curr->children[j];

					if (strcpy(sub, elem->entry.name) == 0)
					{
						curr = elem;
						index = j;
						break;
					}
				}

				delete[] sub;
			}
		}
	}

	curr->parent->children.erase(index);
	delete curr;
}

const char* FAT32::GetCurrentDirectory() const
{
	char* name = "~";
	size_t counter = 1;

	vector<FAT32_FolderStructure*> structs;
	FAT32_FolderStructure* curr = current;
	while (curr->parent)
	{
		structs.push_back(curr);
		curr = curr->parent;
	}

	for (size_t i = structs.size() - 1; i >= 0; --i)
	{
		char* nm = structs[i]->entry.name;
		size_t len = strlen(nm);
		char* curr = new char[len + 2];
		curr[0] = '/';
		memcpy(&curr[1], nm, len);
		curr[len + 1] = 0;

		char* new_name = new char[counter + len + 2 + 1];
		memcpy(new_name, name, counter);
		memcpy(&new_name[counter], curr, len + 2);
		new_name[counter + len + 2] = 0;
		counter += (len + 3);
		delete[] name;
		delete[] curr;
		name = new_name;
	}
	
	return name;
}

void FAT32::ListCurrent()
{
	for (size_t i = 0; i < current->children.size(); i++)
	{
		auto elem = current->children[i];

		//No need to display the parent and self directory
		if (elem->entry.name[0] == '.')
		{
			continue;
		}

		char* flags = new char[7];
		flags[7] = 0;
		if (elem->entry.attributes & FILE_READ_ONLY) flags[0] = '-'; else flags[0] = 'w';
		if (elem->entry.attributes & FILE_HIDDEN) flags[1] = 'h'; else flags[1] = '-';
		if (elem->entry.attributes & FILE_SYSTEM) flags[2] = 's'; else flags[2] = '-';
		if (elem->entry.attributes & FILE_VOLUME_ID) flags[3] = 'v'; else flags[3] = '-';
		if (elem->entry.attributes & FILE_DIRECTORY) flags[4] = 'd'; else flags[4] = '-';
		if (elem->entry.attributes & FILE_ARCHIVE) flags[5] = 'a'; else flags[5] = '-';
		kprintf("%s - %s\n", elem->entry.name, flags);
		delete[] flags;
	}
}

void FAT32::GoTo(char* name)
{
	size_t len = strlen(name);
	if ((memcmp(name, (char*)".", 1) == 0) && (len == 1))
	{
		if (current->parent)
		{
			current = current->parent;
		}

		return;
	}
	if ((memcmp(name, (char*)"..", 2) == 0) && (len == 2))
	{
		return;
	}

	for (size_t i = 0; i < current->children.size(); i++)
	{
		auto elem = current->children[i];

		if (strcmp(elem->entry.name, name) == 0)
		{
			if ((elem->entry.attributes & FILE_DIRECTORY) & FILE_DIRECTORY)
			{
				current = elem;
			}

			break;
		}
	}
}

FAT32_FolderStructure* FAT32::RetrieveFromRoot(DirEntry entry)
{
	vector<FAT32_FolderStructure*> all = GetAllEntries(root);
	for (size_t i = 0; i < all.size(); i++)
	{
		auto elem = all[i];

		if (elem->entry.cluster == entry.cluster)
		{
			return elem;
		}
	}

	return nullptr;
}

FAT32_FolderStructure* FAT32::GetFolderByCluster(uint32_t cluster)
{
	if (root->entry.cluster == cluster)
	{
		return root;
	}

	vector<FAT32_FolderStructure*> all = GetAllEntries(root);
	for (size_t i = 0; i < all.size(); i++)
	{
		auto elem = all[i];

		if (elem->entry.cluster == cluster)
		{
			return elem;
		}
	}

	return nullptr;
}

vector<FAT32_FolderStructure*> FAT32::GetAllEntries(FAT32_FolderStructure* top)
{
	vector<FAT32_FolderStructure*> res = top->children;

	for (size_t i = 0; i < top->children.size(); i++)
	{
		auto elem = top->children[i];

		vector<FAT32_FolderStructure*> ret = GetAllEntries(elem);
		for (size_t j = 0; j < ret.size(); j++)
		{
			res.push_back(ret[j]);
		}
	}

	return res;
}