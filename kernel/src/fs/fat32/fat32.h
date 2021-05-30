#ifndef FAT32_H
#define FAT32_H

#include "fs/filesystem.h"
#include "fat32driver.h"

struct FAT32_FolderStructure
{
    ~FAT32_FolderStructure();

	FAT32_FolderStructure* parent;
	DirEntry entry;

	vector<FAT32_FolderStructure*> children;
};

struct FAT32_ActiveFile : public ActiveFile
{
	FAT32_FolderStructure* file;
	const char* path;
};

class FAT32 : public Filesystem
{
public:
    FAT32(Device* device);
    ~FAT32();

    FAT32_ActiveFile* OpenFile(const char* path);
	FAT32_ActiveFile* CreateFile(const char* path, const char* fileName, uint8_t attributes, uint64_t size);
	void DeleteFile(FAT32_ActiveFile* file);
	void CloseFile(FAT32_ActiveFile* file);

	int ReadFile(FAT32_ActiveFile* file, void* buffer, uint64_t nBytes);
	int WriteFile(FAT32_ActiveFile* file, void* buffer, uint64_t nBytes);
	int ResizeFile(FAT32_ActiveFile* file, uint64_t new_size);

	FAT32_FolderStructure* GetRoot() const { return root; }
	FAT32_FolderStructure* GetCurrent() const { return current; }

	void AddToRecords(const char* path, DirEntry entry);
	void DeleteFromRecords(const char* name);

	const char* GetCurrentDirectory() const;

	FAT32Driver* GetDriver() { return driver; }

	void ListCurrent();
	void GoTo(char* name);

private:
	FAT32_FolderStructure* RetrieveFromRoot(DirEntry entry);

	FAT32_FolderStructure* GetFolderByCluster(uint32_t cluster);
	vector<FAT32_FolderStructure*> GetAllEntries(FAT32_FolderStructure* top);

	FAT32Driver* driver;

	FAT32_FolderStructure* root;
	FAT32_FolderStructure* current;
};

#endif