#include "fat32.h"

#include "lib/memory.h"
#include "lib/string.h"
#include "lib/stdio.h"

extern bool fromUEFI;

void LocateFilesystemsFAT32(Device* device)
{
    if (!device)
    {
        return;
    }

    //The MBR is still at 0x0600, extract the boot partition's start LBA
    uint32_t* MBR = (uint32_t*)0x0600;

    //On UEFI, we don't have partitions for now
    if (!fromUEFI)
    {
        uint64_t BaseOffset = 446;
        Partition* part = (Partition*)((uint64_t)MBR + BaseOffset); //the 2nd partition contains the FAT32 filesystem

        for (uint32_t i = 0; i < 4; i++)
        {
            if (part->filesystem == 0x0B)
            {
                RegisterFilesystem(new FAT32(device, part->LBA_start));
            }

            part++;
        }
    }
    else
    {
        RegisterFilesystem(new FAT32(device, 0));
    }
}

uint64_t FAT32_ActiveFile::GetSize() const
{
    return entry.size;
}

const char* FAT32_ActiveFile::GetName() const
{
    return entry.name;
}

const char* FAT32_ActiveFile::GetPath() const
{
    return path;
}

uint32_t FAT32_ActiveFile::GetAttributes() const
{
    return (uint32_t)entry.attributes;
}

FAT32::FAT32(Device* device, uint32_t part_start) : Filesystem(device, part_start)
{
    backing = device;
    start = part_start;
    FAT32_BootSector boot = {};
    uint32_t sectors =
        device && device->Read(part_start, &boot, 1) && ValidateFAT32BootSector(boot, part_start, device->SectorCount())
            ? (boot.TotalSectors ? boot.TotalSectors : boot.LargeTotalSectors)
            : 0;
    journal = new JournalDevice(device, part_start, sectors);
    driver = new FAT32Driver(journal, part_start);
}

FAT32::~FAT32()
{
    delete driver;
    delete journal;
}

ActiveFile* FAT32::OpenFile(const char* path)
{
    ActiveFile* file = nullptr;
    Lookup(path, file);
    return file;
}

int FAT32::Lookup(const char* path, ActiveFile*& result)
{
    result = nullptr;
    DirEntry entry;
    int ret = driver->OpenFile(path, &entry);
    if (ret != 0)
    {
        return ret;
    }

    char* ptr = new char[strlen((char*)path) + 1];
    if (!ptr)
    {
        return -1;
    }

    strcpy(ptr, (char*)path);

    FAT32_ActiveFile* file = new FAT32_ActiveFile();
    if (!file)
    {
        delete[] ptr;
        return -1;
    }
    file->seek_position = 0;
    file->path = ptr;
    file->entry = entry;

    result = file;
    return 0;
}

int FAT32::MakeDirectory(const char* path, const FilePermissions* permissions)
{
    if (!journal->Begin())
    {
        return -1;
    }

    return Finish(driver->MakeDirectory(path, permissions));
}

int FAT32::RemoveFile(const char* path)
{
    if (!journal->Begin())
    {
        return -1;
    }

    return Finish(driver->RemoveFile(path));
}

int FAT32::MoveFile(const char* source, const char* destination)
{
    if (!journal->Begin())
    {
        return -1;
    }

    return Finish(driver->MoveFile(source, destination));
}

int FAT32::SaveFile(const char* path, const void* data, uint32_t size, bool create, const FilePermissions* permissions)
{
    if (!journal->Begin())
    {
        return -1;
    }

    return Finish(driver->SaveFile(path, data, size, create, permissions));
}

void FAT32::CloseFile(ActiveFile* file)
{
    delete[] ((FAT32_ActiveFile*)file)->path;
    delete file;
}

ActiveFile* FAT32::CreateFile(const char* path, const char* name, permissions_t permissions)
{
    if (!path || !name || strlen((char*)name) >= sizeof(DirEntry::name))
    {
        return nullptr;
    }

    uint32_t size = 0;
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
    entry.permissions.mode = attributes & DIRECTORY ? 0755 : 0644;
    if (!journal->Begin())
    {
        return nullptr;
    }

    int ret = Finish(driver->CreateFile(path, &entry));

    if (ret != 0)
    {
        return nullptr;
    }

    size_t pathLen = strlen((char*)path);
    size_t fnameLen = strlen((char*)name);
    size_t total = pathLen + fnameLen + 2;
    char* ptr = new char[total];
    if (!ptr)
    {
        return nullptr;
    }

    memcpy(ptr, path, pathLen);
    ptr[pathLen] = '/';
    memcpy(&ptr[pathLen + 1], name, fnameLen);
    ptr[pathLen + 1 + fnameLen] = 0;

    FAT32_ActiveFile* file = new FAT32_ActiveFile();
    if (!file)
    {
        delete[] ptr;
        return nullptr;
    }

    file->path = ptr;
    file->entry = entry;

    return (ActiveFile*)file;
}

int FAT32::ResizeFile(ActiveFile* file, uint64_t new_size)
{
    if (!file || new_size > 0xffffffff || !journal->Begin())
    {
        return -1;
    }

    auto& entry = ((FAT32_ActiveFile*)file)->entry;
    auto original = entry;
    int status = Finish(driver->ResizeFile(entry, new_size));
    if (!status)
    {
        file->seek_position = 0;
    }
    else
    {
        entry = original;
    }

    return status;
}

int FAT32::DeleteFile(ActiveFile* file)
{
    if (!file || !journal->Begin())
    {
        return -1;
    }

    int status = Finish(driver->DeleteFile(((FAT32_ActiveFile*)file)->entry));
    CloseFile(file);
    return status;
}

uint64_t FAT32::GetDirectoryEntryCount(const char* path)
{
    DirEntry entry;
    int ret = driver->OpenFile(path, &entry);
    if (ret != 0 || !(entry.attributes & FILE_DIRECTORY))
    {
        return 0;
    }

    vector<DirEntry> entries = driver->GetDirectories(entry.cluster, 0, false);
    return entries.size();
}

ActiveFile* FAT32::GetFile(const char* path, uint64_t index)
{
    DirEntry entry;
    if (driver->OpenFile(path, &entry) || !(entry.attributes & FILE_DIRECTORY))
    {
        return nullptr;
    }

    vector<DirEntry> entries = driver->GetDirectories(entry.cluster, 0, false);

    if (index < entries.size())
    {
        DirEntry file = entries[index];

        size_t pathlen = strlen((char*)path);
        size_t namelen = strlen(file.name);
        size_t totalLen = pathlen + namelen + 1;

        char* totalPath = new char[totalLen + 1];
        if (!totalPath)
        {
            return nullptr;
        }

        memcpy(totalPath, path, pathlen);
        totalPath[pathlen] = '/';
        memcpy(&totalPath[pathlen + 1], file.name, namelen);
        totalPath[pathlen + namelen + 1] = 0;

        ActiveFile* opened = OpenFile(totalPath);
        if (opened)
        {
            // Keep the enumerated name (including . and ..), while lookup
            // supplies the canonical object's ownership and attributes.
            strcpy(((FAT32_ActiveFile*)opened)->entry.name, file.name);
        }

        delete[] totalPath;
        return opened;
    }

    return nullptr;
}

int FAT32::Read(ActiveFile* file, void* buffer, uint64_t nBytes)
{
    if (!file)
    {
        return -1;
    }

    auto& entry = ((FAT32_ActiveFile*)file)->entry;
    uint64_t available = file->seek_position < entry.size ? entry.size - file->seek_position : 0;
    if (nBytes > available)
    {
        nBytes = available;
    }

    int result = driver->ReadFile(entry, file->seek_position, buffer, nBytes);
    if (!result)
    {
        file->seek_position += nBytes;
    }

    return result;
}

int FAT32::Write(ActiveFile* file, void* buffer, uint64_t nBytes)
{
    if (!file)
    {
        return -1;
    }

    if (!journal->Begin())
    {
        return -1;
    }

    auto& entry = ((FAT32_ActiveFile*)file)->entry;
    auto original = entry;
    int result = Finish(driver->WriteFile(entry, file->seek_position, buffer, nBytes));
    if (result)
    {
        entry = original;
    }
    if (!result)
    {
        file->seek_position += nBytes;
    }

    return result;
}

bool FAT32::AccountStorage(uint64_t (&sectors)[2][16], Device*& storage)
{
    bool result = driver->AccountStorage(sectors, storage);
    storage = result ? backing : nullptr;
    return result;
}

FilePermissions FAT32_ActiveFile::GetPermissions() const
{
    return entry.permissions;
}

int FAT32::Finish(int status)
{
    bool flushed = status == 0 && driver->Sync();
    bool committed = journal->Finish(flushed);
    if (!committed && journal->Healthy())
    {
        // Discard all speculative FAT state after validation/full-volume errors.
        delete driver;
        driver = new FAT32Driver(journal, start);
    }

    return committed ? 0 : status ? status : -1;
}

int FAT32::SetPermissions(const char* path, const FilePermissions& permissions)
{
    if (!journal->Begin())
    {
        return -1;
    }

    return Finish(driver->SetPermissions(path, permissions));
}

int FAT32::Flush()
{
    return driver->IsValid() && journal->Flush() ? 0 : -1;
}

int FAT32::DirectoryCount(const char* path, uint64_t& count)
{
    DirEntry entry;
    if (driver->OpenFile(path, &entry) || !(entry.attributes & FILE_DIRECTORY))
    {
        return -1;
    }

    auto entries = driver->GetDirectories(entry.cluster, 0, false);
    count = entries.size();
    return driver->IsValid() ? 0 : -1;
}
