#include "fat32driver.h"
#include "fs/storage/journal.h"
#include "hackos/runtime/path.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "arch/x86_64/timer/rtc.h"

namespace
{
char Upper(char c)
{
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}

bool SameName(const char* a, const char* b)
{
    while (*a && Upper(*a) == Upper(*b))
    {
        ++a;
        ++b;
    }

    return Upper(*a) == Upper(*b);
}

uint8_t Checksum(const DirectoryEntry& entry)
{
    const uint8_t* name = (const uint8_t*)&entry;
    uint8_t sum = 0;
    for (unsigned i = 0; i < 11; ++i)
    {
        sum = ((sum & 1) << 7) + (sum >> 1) + name[i];
    }

    return sum;
}

void ShortName(const DirectoryEntry& entry, char* name)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 8 && entry.name[i] != ' '; ++i)
    {
        char c = entry.name[i];
        if ((entry.reserved & 8) && c >= 'A' && c <= 'Z')
        {
            c += 'a' - 'A';
        }

        name[n++] = c;
    }

    if (entry.ext[0] != ' ')
    {
        name[n++] = '.';
        for (unsigned i = 0; i < 3 && entry.ext[i] != ' '; ++i)
        {
            char c = entry.ext[i];
            if ((entry.reserved & 16) && c >= 'A' && c <= 'Z')
            {
                c += 'a' - 'A';
            }

            name[n++] = c;
        }
    }

    name[n] = 0;
}

bool ShortCharacter(char c)
{
    c = Upper(c);
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '$' || c == '~';
}

void Stamp(DirectoryEntry& entry)
{
    tm time;
    GetTimeRTC(&time);
    unsigned year = time.year + 1900;
    uint16_t date = time.day | ((time.month + 1) << 5) | ((year >= 1980 && year <= 2107 ? year - 1980 : 0) << 9);
    entry.mtime_date = entry.atime_date = entry.ctime_date = date;
    entry.mtime_time = entry.ctime_time = (time.second / 2) | (time.minute << 5) | (time.hour << 11);
}
}

bool ValidateFAT32BootSector(const FAT32_BootSector& b, uint32_t start, uint64_t capacity)
{
    // Block devices currently expose 512-byte sectors only.
    uint64_t first = (uint64_t)b.NumberOfFATs * b.SectorsPerFAT32 + b.ReservedSectors;
    uint64_t total = b.TotalSectors ? b.TotalSectors : b.LargeTotalSectors;
    if (b.BootablePartitionSignature[0] != 0x55 || b.BootablePartitionSignature[1] != 0xaa || b.BytesPerSector != 512 ||
        !b.SectorsPerCluster || b.SectorsPerCluster > 128 || (b.SectorsPerCluster & (b.SectorsPerCluster - 1)) ||
        !b.ReservedSectors || !b.NumberOfFATs || b.NumberOfFATs > 2 || !b.SectorsPerFAT32 || b.SectorsPerFAT ||
        b.RootEntries || b.Version || first >= total || b.ReservedSectors != STORAGE_RESERVED || b.FSInfoSector != 1 ||
        b.BackupBootSector != 6 || start > capacity || total > capacity - start ||
        ((b.Flags & 0x80) && (b.Flags & 0xf) >= b.NumberOfFATs))
    {
        return false;
    }

    uint64_t clusters = (total - first) / b.SectorsPerCluster;
    if (!clusters || clusters >= 0x0ffffff5 || clusters + 2 > (uint64_t)b.SectorsPerFAT32 * 128 || b.RootDirStart < 2 ||
        b.RootDirStart >= clusters + 2)
    {
        return false;
    }

    return true;
}

FAT32Driver::FAT32Driver(Device* device, uint32_t part_start) : device(device), PartitionStart(part_start)
{
    if (!device)
    {
        return;
    }

    BootSector = (FAT32_BootSector*)kmalloc(512);
    if (!BootSector || !device->Read(PartitionStart, BootSector, 1))
    {
        return;
    }
    const auto& b = *BootSector;
    if (!ValidateFAT32BootSector(b, PartitionStart, device->SectorCount()))
    {
        return;
    }

    uint64_t first = (uint64_t)b.NumberOfFATs * b.SectorsPerFAT32 + b.ReservedSectors;
    uint64_t total = b.TotalSectors ? b.TotalSectors : b.LargeTotalSectors;
    uint64_t clusters = (total - first) / b.SectorsPerCluster;
    FirstDataSector = first;
    TotalClusters = clusters;
    ClusterSize = b.SectorsPerCluster * 512;
    RootDirStart = b.RootDirStart;
    temporaryBuffer = (uint8_t*)kmalloc(ClusterSize);
    FATcache = (uint32_t*)kmalloc((uint64_t)b.SectorsPerFAT32 * 512);
    if (!temporaryBuffer || !FATcache)
    {
        return;
    }

    unsigned active = b.Flags & 0x80 ? b.Flags & 0xf : 0;
    if (!device->Read((uint64_t)PartitionStart + b.ReservedSectors + (uint64_t)active * b.SectorsPerFAT32, FATcache,
                      b.SectorsPerFAT32))
    {
        return;
    }
    valid = true;
}

FAT32Driver::~FAT32Driver()
{
    FlushFAT();
    if (temporaryBuffer)
    {
        kfree(temporaryBuffer);
    }

    if (FATcache)
    {
        kfree(FATcache);
    }

    if (BootSector)
    {
        kfree(BootSector);
    }
}

bool FAT32Driver::ValidCluster(uint32_t cluster) const
{
    return valid && cluster >= 2 && cluster - 2 < TotalClusters;
}

uint32_t FAT32Driver::ReadFAT(uint32_t cluster) const
{
    return ValidCluster(cluster) ? FATcache[cluster] & 0x0fffffff : BAD_CLUSTER;
}

void FAT32Driver::WriteFAT(uint32_t cluster, uint32_t value)
{
    if (ioError || !ValidCluster(cluster))
    {
        return;
    }

    FATcache[cluster] = (FATcache[cluster] & 0xf0000000) | (value & 0x0fffffff);
    dirty = true;
}

void FAT32Driver::FlushFAT()
{
    if (!valid || ioError || !dirty)
    {
        return;
    }

    for (unsigned i = 0; i < BootSector->NumberOfFATs; ++i)
    {
        if ((BootSector->Flags & 0x80) && i != (BootSector->Flags & 0xf))
        {
            continue;
        }

        if (!device->Write((uint64_t)PartitionStart + BootSector->ReservedSectors +
                               (uint64_t)i * BootSector->SectorsPerFAT32,
                           FATcache, BootSector->SectorsPerFAT32))
        {
            ioError = true;
            return;
        }
    }

    // Allocation changed: invalidate optional cached free-space hints, including the backup.
    uint32_t infoSector = BootSector->FSInfoSector;
    if (infoSector && infoSector < BootSector->ReservedSectors)
    {
        FSInfo info;
        if (!device->Read((uint64_t)PartitionStart + infoSector, &info, 1))
        {
            ioError = true;
            return;
        }
        if (info.LeadSignature == 0x41615252 && info.StructSignature == 0x61417272 && info.TrailSignature == 0xaa550000)
        {
            info.FreeSpace = info.LastWritten = 0xffffffff;
            if (!device->Write((uint64_t)PartitionStart + infoSector, &info, 1))
            {
                ioError = true;
                return;
            }
            uint32_t backup = BootSector->BackupBootSector;
            if (backup && backup + infoSector < BootSector->ReservedSectors)
            {
                if (!device->Write((uint64_t)PartitionStart + backup + infoSector, &info, 1))
                {
                    ioError = true;
                    return;
                }
            }
        }
    }

    dirty = false;
}

bool FAT32Driver::ReadCluster(uint32_t cluster, void* buffer)
{
    if (ioError || !ValidCluster(cluster) ||
        !device->Read((uint64_t)PartitionStart + FirstDataSector +
                          (uint64_t)(cluster - 2) * BootSector->SectorsPerCluster,
                      buffer, BootSector->SectorsPerCluster))
    {
        ioError = true;
        memset(buffer, 0, ClusterSize);
        return false;
    }

    return true;
}

void FAT32Driver::WriteCluster(uint32_t cluster, void* buffer)
{
    if (ioError || !ValidCluster(cluster) ||
        !device->Write((uint64_t)PartitionStart + FirstDataSector +
                           (uint64_t)(cluster - 2) * BootSector->SectorsPerCluster,
                       buffer, BootSector->SectorsPerCluster))
    {
        ioError = true;
    }
}

vector<uint32_t> FAT32Driver::GetClusterChain(uint32_t start)
{
    vector<uint32_t> chain;
    uint32_t slow = start, fast = start;
    // Floyd's check bounds corrupt/cyclic chains without an allocation proportional to the disk.
    while (ValidCluster(fast))
    {
        fast = ReadFAT(fast);
        if (fast >= END_CLUSTER)
        {
            break;
        }

        if (!ValidCluster(fast))
        {
            return {};
        }

        fast = ReadFAT(fast);
        slow = ReadFAT(slow);
        if (fast == slow)
        {
            return {};
        }
    }

    for (uint32_t count = 0; count < TotalClusters && ValidCluster(start); ++count)
    {
        chain.push_back(start);
        start = ReadFAT(start);
        if (start >= END_CLUSTER)
        {
            return chain;
        }
    }

    return {};
}

uint32_t FAT32Driver::AllocateClusterChain(uint32_t count)
{
    if (!count)
    {
        return 0;
    }

    uint32_t first = 0, previous = 0;
    for (uint32_t c = 2; c - 2 < TotalClusters && count; ++c)
    {
        if (ReadFAT(c) != FREE_CLUSTER)
        {
            continue;
        }

        WriteFAT(c, END_CLUSTER);
        if (previous)
        {
            WriteFAT(previous, c);
        }
        else
        {
            first = c;
        }

        memset(temporaryBuffer, 0, ClusterSize);
        WriteCluster(c, temporaryBuffer);
        previous = c;
        --count;
    }

    if (!count && !ioError)
    {
        return first;
    }

    FreeClusterChain(first);
    return BAD_CLUSTER;
}

void FAT32Driver::FreeClusterChain(uint32_t start)
{
    auto chain = GetClusterChain(start);
    for (size_t i = 0; i < chain.size(); ++i)
    {
        WriteFAT(chain[i], FREE_CLUSTER);
    }
}

vector<DirEntry> FAT32Driver::GetDirectories(uint32_t cluster, uint32_t filter, bool exclude)
{
    vector<DirEntry> entries;
    auto chain = GetClusterChain(cluster);
    if (!chain.size())
    {
        ioError = true;
        return entries;
    }

    char longName[128] = {};
    unsigned expected = 0, count = 0;
    uint8_t checksum = 0;
    uint32_t firstIndex = 0;
    unsigned perCluster = ClusterSize / sizeof(DirectoryEntry);
    for (size_t c = 0; c < chain.size(); ++c)
    {
        if (!ReadCluster(chain[c], temporaryBuffer))
        {
            return {};
        }

        for (unsigned i = 0; i < perCluster; ++i)
        {
            const auto& raw = ((DirectoryEntry*)temporaryBuffer)[i];
            uint8_t first = (uint8_t)raw.name[0];
            if (first == ENTRY_END)
            {
                return entries;
            }

            if (first == ENTRY_FREE)
            {
                expected = count = 0;
                continue;
            }

            if (raw.attributes == FILE_LONG_NAME)
            {
                const auto& lfn = *(const LongDirectoryEntry*)&raw;
                unsigned order = lfn.order & 0x1f;
                if (lfn.order & LAST_LONG_ENTRY)
                {
                    memset(longName, 0, sizeof longName);
                    expected = count = order;
                    checksum = lfn.checksum;
                    firstIndex = c * perCluster + i;
                }

                if (!order || order > 10 || order != expected || lfn.checksum != checksum || lfn.type || lfn.reserved ||
                    (lfn.order & 0xa0))
                {
                    expected = count = 0;
                    continue;
                }

                for (unsigned j = 0; j < 13; ++j)
                {
                    uint16_t ch = j < 5 ? lfn.name0[j] : j < 11 ? lfn.name1[j - 5] : lfn.name2[j - 11];
                    unsigned position = (order - 1) * 13 + j;
                    // The public filename API currently supports ASCII names of at most 127 bytes.
                    if (ch != 0 && ch != 0xffff && (ch > 127 || position >= sizeof longName - 1))
                    {
                        count = 0;
                    }
                    else if (position < sizeof longName - 1)
                    {
                        longName[position] = ch == 0xffff ? 0 : ch;
                    }
                }

                --expected;
                continue;
            }

            DirEntry entry;
            bool hasLong = count && !expected && Checksum(raw) == checksum && longName[0];
            if (hasLong)
            {
                strcpy(entry.name, longName);
            }
            else
            {
                ShortName(raw, entry.name);
            }

            entry.cluster = (raw.clusterLow | ((uint32_t)raw.clusterHigh << 16)) & 0x0fffffff;
            entry.size = raw.fileSize;
            entry.attributes = raw.attributes;
            entry.parentCluster = chain[c];
            entry.offsetInParentCluster = i;
            entry.directoryCluster = cluster;
            entry.firstDirectoryIndex = hasLong ? firstIndex : c * perCluster + i;
            entry.directoryEntryCount = hasLong ? count + 1 : 1;
            count = expected = 0;
            if (raw.attributes & FILE_VOLUME_ID)
            {
                continue;
            }

            if (!SameName(entry.name, ".") && !SameName(entry.name, "..") && !Permissions(entry, false))
            {
                ioError = true;
                return {};
            }

            if ((!exclude && !(entry.attributes & filter)) || (exclude && (entry.attributes & filter)))
            {
                entries.push_back(entry);
            }
        }
    }

    return entries;
}

int FAT32Driver::DirectorySearch(const char* name, uint32_t cluster, DirEntry* entry)
{
    if (!ValidCluster(cluster))
    {
        return -1;
    }

    auto entries = GetDirectories(cluster, 0, false);
    if (ioError)
    {
        return -1;
    }

    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (SameName(entries[i].name, name))
        {
            if (entry)
            {
                *entry = entries[i];
            }

            return 0;
        }
    }

    return FAT32_ERROR_FILE_NOT_FOUND;
}

int FAT32Driver::OpenFile(const char* path, DirEntry* entry)
{
    if (!valid || ioError || !path || !entry || path[0] != '~' || (path[1] && path[1] != '/'))
    {
        return -1;
    }

    char normalized[256];
    if (!NormalizePath(path, "~", normalized, true))
    {
        return -1;
    }

    path = normalized;
    DirEntry current;
    current.cluster = RootDirStart;
    current.attributes = FILE_DIRECTORY;
    strcpy(current.name, "~");
    ++path;
    while (*path)
    {
        while (*path == '/')
        {
            ++path;
        }

        if (!*path)
        {
            break;
        }

        char part[128];
        unsigned n = 0;
        while (*path && *path != '/')
        {
            if (n == sizeof part - 1)
            {
                return -1;
            }

            part[n++] = *path++;
        }

        part[n] = 0;
        if (!(current.attributes & FILE_DIRECTORY))
        {
            return FAT32_ERROR_NOT_DIRECTORY;
        }

        if (SameName(part, "."))
        {
            continue;
        }

        if (SameName(part, "..") && current.cluster == RootDirStart)
        {
            continue;
        }

        int status = DirectorySearch(part, current.cluster, &current);
        if (status)
        {
            return status;
        }

        if ((current.attributes & FILE_DIRECTORY) && !current.cluster)
        {
            current.cluster = RootDirStart;
        }
    }

    if (!current.parentCluster && !Permissions(current, false))
    {
        return -1;
    }

    *entry = current;
    return 0;
}

int FAT32Driver::UpdateEntry(const DirEntry& entry, bool remove)
{
    if (ioError || !ValidCluster(entry.parentCluster) || entry.offsetInParentCluster >= ClusterSize / 32)
    {
        return -1;
    }

    if (remove)
    {
        auto chain = GetClusterChain(entry.directoryCluster);
        unsigned perCluster = ClusterSize / 32;
        uint64_t end = (uint64_t)entry.firstDirectoryIndex + entry.directoryEntryCount;
        if (!entry.directoryEntryCount || end > chain.size() * perCluster)
        {
            return -1;
        }

        // Remove the owning short entry first; never free data while it is live.
        for (uint64_t cursor = end; cursor > entry.firstDirectoryIndex;)
        {
            uint64_t i = --cursor;
            ReadCluster(chain[i / perCluster], temporaryBuffer);
            temporaryBuffer[(i % perCluster) * 32] = ENTRY_FREE;
            WriteCluster(chain[i / perCluster], temporaryBuffer);
        }
    }
    else
    {
        ReadCluster(entry.parentCluster, temporaryBuffer);
        auto& raw = ((DirectoryEntry*)temporaryBuffer)[entry.offsetInParentCluster];
        raw.clusterLow = entry.cluster & 0xffff;
        raw.clusterHigh = entry.cluster >> 16;
        raw.fileSize = entry.size;
        raw.attributes = entry.attributes;
        WriteCluster(entry.parentCluster, temporaryBuffer);
    }

    return ioError ? -1 : 0;
}

int FAT32Driver::DirectoryAdd(uint32_t cluster, DirEntry& file)
{
    auto chain = GetClusterChain(cluster);
    if (!chain.size())
    {
        return -1;
    }

    unsigned length = strlen(file.name), base = length, ext = length;
    for (unsigned i = 0; i < length; ++i)
    {
        if (file.name[i] == '.')
        {
            base = i;
            ext = i + 1;
        }
    }

    DirectoryEntry raw = {};
    uint8_t* shortName = (uint8_t*)&raw;
    memset(shortName, ' ', 11);
    bool simple = base > 0 && base <= 8 && length - ext <= 3;
    for (unsigned i = 0; i < length; ++i)
    {
        if (i == base && ext != length)
        {
            continue;
        }

        if (!ShortCharacter(file.name[i]) || Upper(file.name[i]) != file.name[i])
        {
            simple = false;
        }
    }

    if (simple)
    {
        for (unsigned i = 0; i < base; ++i)
        {
            shortName[i] = file.name[i];
        }

        for (unsigned i = ext; i < length; ++i)
        {
            shortName[8 + i - ext] = file.name[i];
        }
    }
    else
    {
        // Generate a unique 8.3 alias for the ASCII long name.
        bool found = false;
        for (unsigned serial = 1; serial < 1000000; ++serial)
        {
            memset(shortName, ' ', 11);
            char digits[6];
            unsigned n = 0, v = serial;
            do
            {
                digits[n++] = '0' + v % 10;
                v /= 10;
            } while (v);
            unsigned used = 0;
            for (unsigned i = 0; i < base && used < 7 - n; ++i)
            {
                if (ShortCharacter(file.name[i]))
                {
                    shortName[used++] = Upper(file.name[i]);
                }
            }

            if (!used)
            {
                shortName[used++] = '_';
            }

            shortName[used++] = '~';
            while (n)
            {
                shortName[used++] = digits[--n];
            }

            for (unsigned i = ext; i < length && i - ext < 3; ++i)
            {
                shortName[8 + i - ext] = ShortCharacter(file.name[i]) ? Upper(file.name[i]) : '_';
            }

            bool collision = false;
            for (size_t c = 0; c < chain.size() && !collision; ++c)
            {
                ReadCluster(chain[c], temporaryBuffer);
                for (unsigned i = 0; i < ClusterSize; i += 32)
                {
                    if (!memcmp(temporaryBuffer + i, shortName, 11))
                    {
                        collision = true;
                        break;
                    }
                }
            }

            if (!collision)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            return FAT32_ERROR_NO_FREE_SPACE;
        }
    }

    raw.attributes = file.attributes;
    raw.fileSize = file.size;
    raw.clusterLow = file.cluster & 0xffff;
    raw.clusterHigh = file.cluster >> 16;
    Stamp(raw);
    unsigned longCount = simple ? 0 : (length + 12) / 13;
    unsigned needed = longCount + 1, perCluster = ClusterSize / 32;
    uint64_t first = 0, freeCount = 0, endIndex = (uint64_t)-1;
    bool found = false;
    for (size_t c = 0; c < chain.size() && !found; ++c)
    {
        ReadCluster(chain[c], temporaryBuffer);
        for (unsigned i = 0; i < perCluster; ++i)
        {
            uint64_t index = c * perCluster + i;
            unsigned ch = temporaryBuffer[i * 32];
            if (ch == ENTRY_END && endIndex == (uint64_t)-1)
            {
                endIndex = index;
            }

            if (index >= endIndex || ch == ENTRY_FREE)
            {
                if (!freeCount)
                {
                    first = index;
                }

                if (++freeCount == needed)
                {
                    found = true;
                    break;
                }
            }
            else
            {
                freeCount = 0;
            }
        }
    }

    if (!found)
    {
        if (!freeCount)
        {
            first = chain.size() * perCluster;
        }

        unsigned extra = (needed - freeCount + perCluster - 1) / perCluster;
        uint32_t added = AllocateClusterChain(extra);
        if (added == BAD_CLUSTER)
        {
            return FAT32_ERROR_NO_FREE_SPACE;
        }

        WriteFAT(chain[chain.size() - 1], added);
        chain = GetClusterChain(cluster);
        if (!Sync())
        {
            return -1;
        }
    }

    for (unsigned j = 0; j < needed; ++j)
    {
        uint64_t index = first + j;
        ReadCluster(chain[index / perCluster], temporaryBuffer);
        void* slot = temporaryBuffer + (index % perCluster) * 32;
        if (j == longCount)
        {
            memcpy(slot, &raw, 32);
        }
        else
        {
            LongDirectoryEntry lfn = {};
            unsigned order = longCount - j;
            lfn.order = order | (j == 0 ? LAST_LONG_ENTRY : 0);
            lfn.attributes = FILE_LONG_NAME;
            lfn.checksum = Checksum(raw);
            for (unsigned k = 0; k < 13; ++k)
            {
                unsigned pos = (order - 1) * 13 + k;
                uint16_t ch = pos < length ? (uint8_t)file.name[pos] : pos == length ? 0 : 0xffff;
                if (k < 5)
                {
                    lfn.name0[k] = ch;
                }
                else if (k < 11)
                {
                    lfn.name1[k - 5] = ch;
                }
                else
                {
                    lfn.name2[k - 11] = ch;
                }
            }

            memcpy(slot, &lfn, 32);
        }

        WriteCluster(chain[index / perCluster], temporaryBuffer);
    }

    uint64_t after = first + needed;
    if (after > endIndex && after < chain.size() * perCluster)
    {
        ReadCluster(chain[after / perCluster], temporaryBuffer);
        temporaryBuffer[(after % perCluster) * 32] = ENTRY_END;
        WriteCluster(chain[after / perCluster], temporaryBuffer);
    }

    file.parentCluster = chain[(after - 1) / perCluster];
    file.offsetInParentCluster = (after - 1) % perCluster;
    file.directoryCluster = cluster;
    file.firstDirectoryIndex = first;
    file.directoryEntryCount = needed;
    return !ioError && Permissions(file, true) ? 0 : -1;
}

int FAT32Driver::CreateFile(const char* parentPath, DirEntry* file)
{
    DirEntry parent;
    if (!file || OpenFile(parentPath, &parent) || !(parent.attributes & FILE_DIRECTORY))
    {
        return -1;
    }

    size_t length = strlen(file->name);
    if (!length || length >= sizeof file->name || SameName(file->name, ".") || SameName(file->name, "..") ||
        file->name[length - 1] == '.' || file->name[length - 1] == ' ')
    {
        return -1;
    }

    for (size_t i = 0; i < length; ++i)
    {
        char ch = file->name[i];
        if (ch < 32 || ch > 126 || ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' ||
            ch == '<' || ch == '>' || ch == '|')
        {
            return -1;
        }
    }

    if (DirectorySearch(file->name, parent.cluster, nullptr) != FAT32_ERROR_FILE_NOT_FOUND)
    {
        return -1;
    }

    unsigned count = ((uint64_t)file->size + ClusterSize - 1) / ClusterSize;
    if (file->attributes & FILE_DIRECTORY)
    {
        count = 1;
        file->size = 0;
    }

    file->cluster = AllocateClusterChain(count);
    if (file->cluster == BAD_CLUSTER)
    {
        return FAT32_ERROR_NO_FREE_SPACE;
    }

    if (file->attributes & FILE_DIRECTORY)
    {
        memset(temporaryBuffer, 0, ClusterSize);
        auto* dots = (DirectoryEntry*)temporaryBuffer;
        for (unsigned i = 0; i < 2; ++i)
        {
            memset(&dots[i], ' ', 11);
            dots[i].name[0] = '.';
            if (i)
            {
                dots[i].name[1] = '.';
            }

            dots[i].attributes = FILE_DIRECTORY;
            uint32_t c = i ? (parent.cluster == RootDirStart ? 0 : parent.cluster) : file->cluster;
            dots[i].clusterLow = c;
            dots[i].clusterHigh = c >> 16;
            Stamp(dots[i]);
        }

        WriteCluster(file->cluster, temporaryBuffer);
    }

    if (!Sync())
    {
        return -1;
    }

    int result = DirectoryAdd(parent.cluster, *file);
    if (result)
    {
        FreeClusterChain(file->cluster);
    }

    return Sync() ? result : -1;
}

int FAT32Driver::DeleteFile(DirEntry entry)
{
    if (!valid || ioError || !entry.parentCluster || (entry.attributes & FILE_READ_ONLY))
    {
        return -1;
    }

    if (entry.attributes & FILE_DIRECTORY)
    {
        auto children = GetDirectories(entry.cluster, 0, false);
        // Recursive removal of corrupt directory graphs is unsafe; require an empty directory.
        for (size_t i = 0; i < children.size(); ++i)
        {
            if (!SameName(children[i].name, ".") && !SameName(children[i].name, ".."))
            {
                return -1;
            }
        }
    }

    if (UpdateEntry(entry, true) || !Sync())
    {
        return -1;
    }

    if (!Permissions(entry, true, true))
    {
        return -1;
    }

    FreeClusterChain(entry.cluster);
    return Sync() ? 0 : -1;
}

int FAT32Driver::ReadFile(DirEntry entry, uint64_t offset, void* buffer, uint64_t bytes)
{
    if (!valid || ioError || (entry.attributes & FILE_DIRECTORY) || (!buffer && bytes))
    {
        return -1;
    }

    if (offset >= entry.size || !bytes)
    {
        return 0;
    }

    if (bytes > entry.size - offset)
    {
        bytes = entry.size - offset;
    }

    auto chain = GetClusterChain(entry.cluster);
    if ((uint64_t)chain.size() * ClusterSize < offset + bytes)
    {
        return -1;
    }

    auto* dest = (uint8_t*)buffer;
    while (bytes)
    {
        unsigned within = offset % ClusterSize;
        uint64_t count = ClusterSize - within;
        if (count > bytes)
        {
            count = bytes;
        }

        if (!ReadCluster(chain[offset / ClusterSize], temporaryBuffer))
        {
            return -1;
        }

        memcpy(dest, temporaryBuffer + within, count);
        dest += count;
        offset += count;
        bytes -= count;
    }

    return 0;
}

int FAT32Driver::ResizeFile(DirEntry& entry, uint32_t size)
{
    if (!valid || ioError || (entry.attributes & (FILE_DIRECTORY | FILE_READ_ONLY)) || !entry.parentCluster)
    {
        return -1;
    }

    auto chain = GetClusterChain(entry.cluster);
    if ((entry.cluster && !chain.size()) || (uint64_t)chain.size() * ClusterSize < entry.size)
    {
        return -1;
    }

    unsigned needed = ((uint64_t)size + ClusterSize - 1) / ClusterSize;
    if (needed > chain.size())
    {
        uint32_t extra = AllocateClusterChain(needed - chain.size());
        if (extra == BAD_CLUSTER)
        {
            return FAT32_ERROR_NO_FREE_SPACE;
        }

        if (chain.size())
        {
            WriteFAT(chain[chain.size() - 1], extra);
        }
        else
        {
            entry.cluster = extra;
        }
    }
    else if (needed < chain.size())
    {
        if (needed)
        {
            WriteFAT(chain[needed - 1], END_CLUSTER);
        }
        else
        {
            entry.cluster = 0;
        }

        for (size_t i = needed; i < chain.size(); ++i)
        {
            WriteFAT(chain[i], FREE_CLUSTER);
        }
    }

    // Newly exposed bytes in a previously allocated tail must also start at zero.
    if (size > entry.size && chain.size() && entry.size % ClusterSize)
    {
        unsigned within = entry.size % ClusterSize;
        uint64_t count = ClusterSize - within;
        if (count > size - entry.size)
        {
            count = size - entry.size;
        }

        uint32_t tail = chain[entry.size / ClusterSize];
        ReadCluster(tail, temporaryBuffer);
        memset(temporaryBuffer + within, 0, count);
        WriteCluster(tail, temporaryBuffer);
    }

    entry.size = size;
    FlushFAT();
    return UpdateEntry(entry);
}

int FAT32Driver::WriteFile(DirEntry& entry, uint64_t offset, void* buffer, uint64_t bytes)
{
    if (!valid || ioError || (entry.attributes & (FILE_DIRECTORY | FILE_READ_ONLY)) || (!buffer && bytes))
    {
        return -1;
    }

    if (!bytes)
    {
        return 0;
    }

    if (offset > 0xffffffff || bytes > 0xffffffff - offset)
    {
        return -1;
    }

    if (offset + bytes > entry.size && ResizeFile(entry, offset + bytes))
    {
        return -1;
    }

    auto chain = GetClusterChain(entry.cluster);
    if ((uint64_t)chain.size() * ClusterSize < offset + bytes)
    {
        return -1;
    }

    auto* source = (uint8_t*)buffer;
    while (bytes)
    {
        unsigned within = offset % ClusterSize;
        uint64_t count = ClusterSize - within;
        if (count > bytes)
        {
            count = bytes;
        }

        uint32_t cluster = chain[offset / ClusterSize];
        ReadCluster(cluster, temporaryBuffer);
        memcpy(temporaryBuffer + within, source, count);
        WriteCluster(cluster, temporaryBuffer);
        source += count;
        offset += count;
        bytes -= count;
    }

    return UpdateEntry(entry);
}

bool FAT32Driver::AccountStorage(uint64_t (&sectors)[2][16], Device*& storage)
{
    if (!valid || ioError)
    {
        return false;
    }

    const char* paths[] = {"~/SYSTEM/ACCT0.DAT", "~/SYSTEM/ACCT1.DAT"};
    for (unsigned slot = 0; slot < 2; ++slot)
    {
        DirEntry entry;
        if (OpenFile(paths[slot], &entry) != 0 || ioError || entry.size != 8192 ||
            (entry.attributes & (FILE_DIRECTORY | FILE_VOLUME_ID)))
        {
            return false;
        }

        auto chain = GetClusterChain(entry.cluster);
        if (chain.size() != (8192 + ClusterSize - 1) / ClusterSize)
        {
            return false;
        }

        for (unsigned sector = 0; sector < 16; ++sector)
        {
            uint32_t cluster = chain[sector / BootSector->SectorsPerCluster];
            sectors[slot][sector] = (uint64_t)PartitionStart + FirstDataSector +
                                    (uint64_t)(cluster - 2) * BootSector->SectorsPerCluster +
                                    sector % BootSector->SectorsPerCluster;
            for (unsigned previous = 0; previous < slot * 16 + sector; ++previous)
            {
                if (sectors[previous / 16][previous % 16] == sectors[slot][sector])
                {
                    return false;
                }
            }
        }
    }

    storage = device;
    return true;
}

bool FAT32Driver::Sync()
{
    FlushFAT();
    if (!valid || ioError)
    {
        return false;
    }

    if (!device->Flush())
    {
        ioError = true;
        return false;
    }

    return true;
}

int FAT32Driver::Destination(const char* path, DirEntry& parent, DirEntry& file)
{
    if (!valid || ioError || !path)
    {
        return -1;
    }

    unsigned length = strlen((char*)path), split = length;
    if (length >= 256)
    {
        return -1;
    }

    while (split && path[split - 1] != '/')
    {
        --split;
    }

    if (split < 2 || split == length || length - split >= sizeof(file.name))
    {
        return -1;
    }

    char directory[256];
    memcpy(directory, path, split - 1);
    directory[split - 1] = 0;
    if (OpenFile(directory, &parent) || !(parent.attributes & FILE_DIRECTORY) || (parent.attributes & FILE_READ_ONLY))
    {
        return -1;
    }

    const char* name = path + split;
    if (SameName(name, ".") || SameName(name, "..") || name[length - split - 1] == '.' ||
        name[length - split - 1] == ' ')
    {
        return -1;
    }

    for (unsigned i = 0; name[i]; ++i)
    {
        char ch = name[i];
        if (ch < 32 || ch > 126 || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' ||
            ch == '>' || ch == '|')
        {
            return -1;
        }
    }

    strcpy(file.name, name);
    return DirectorySearch(name, parent.cluster, nullptr) == FAT32_ERROR_FILE_NOT_FOUND ? 0 : -1;
}

int FAT32Driver::MakeDirectory(const char* path, const FilePermissions* permissions)
{
    DirEntry parent, file;
    if (Destination(path, parent, file))
    {
        return -1;
    }

    char directory[256];
    unsigned length = strlen((char*)path) - strlen(file.name) - 1;
    memcpy(directory, path, length);
    directory[length] = 0;
    file.attributes = FILE_DIRECTORY;
    if (permissions)
    {
        file.permissions = *permissions;
    }
    return CreateFile(directory, &file);
}

int FAT32Driver::RemoveFile(const char* path)
{
    DirEntry file;
    if (OpenFile(path, &file) || (file.attributes & FILE_DIRECTORY))
    {
        return -1;
    }

    return DeleteFile(file);
}

int FAT32Driver::MoveFile(const char* source, const char* destination)
{
    DirEntry original, parent, moved;
    if (OpenFile(source, &original) || (original.attributes & (FILE_DIRECTORY | FILE_READ_ONLY)) ||
        Destination(destination, parent, moved))
    {
        return -1;
    }

    moved.cluster = original.cluster;
    moved.size = original.size;
    moved.attributes = original.attributes;
    moved.permissions = original.permissions;
    if (DirectoryAdd(parent.cluster, moved) || !Sync())
    {
        return -1;
    }

    // Stage the destination and source removal in one outer transaction.
    // No data clusters are freed; commit/replay publishes the complete move.
    if (UpdateEntry(original, true) || !Permissions(original, true, true) || !Sync())
    {
        return -1;
    }

    return 0;
}

int FAT32Driver::SaveFile(const char* path, const void* data, uint32_t size, bool create,
                          const FilePermissions* permissions)
{
    if ((!data && size) || !valid || ioError)
    {
        return -1;
    }

    DirEntry original, parent, replacement;
    if (create)
    {
        if (Destination(path, parent, replacement))
        {
            return -1;
        }

        replacement.attributes = FILE_ARCHIVE;
        replacement.permissions.mode = 0644;
        if (permissions)
        {
            replacement.permissions = *permissions;
        }
    }
    else
    {
        if (OpenFile(path, &original) || (original.attributes & (FILE_DIRECTORY | FILE_READ_ONLY)))
        {
            return -1;
        }

        auto oldChain = GetClusterChain(original.cluster);
        if ((original.cluster && !oldChain.size()) || (uint64_t)oldChain.size() * ClusterSize < original.size)
        {
            return -1;
        }

        replacement = original;
    }

    // Stage a fresh chain and its payload before changing the directory.
    // The outer transaction publishes data, FAT and metadata together.
    replacement.cluster = AllocateClusterChain(((uint64_t)size + ClusterSize - 1) / ClusterSize);
    if (replacement.cluster == BAD_CLUSTER)
    {
        Sync();
        return -1;
    }

    replacement.size = size;
    auto chain = GetClusterChain(replacement.cluster);
    uint32_t offset = 0;
    for (unsigned i = 0; i < chain.size(); ++i)
    {
        uint32_t count = size - offset;
        if (count > ClusterSize)
        {
            count = ClusterSize;
        }

        memset(temporaryBuffer, 0, ClusterSize);
        memcpy(temporaryBuffer, (const uint8_t*)data + offset, count);
        WriteCluster(chain[i], temporaryBuffer);
        offset += count;
    }

    if (offset != size || !Sync())
    {
        return -1;
    }

    int status = create ? DirectoryAdd(parent.cluster, replacement) : UpdateEntry(replacement);
    if (status && create && !ioError)
    {
        // A clean directory-capacity failure happened before publication.
        // Return the staged data chain so repeated failed saves cannot fill it.
        FreeClusterChain(replacement.cluster);
        Sync();
        return -1;
    }

    if (status || !Sync())
    {
        // The adapter discards this speculative chain and reloads the FAT
        // when staging fails. Only its final commit writes home sectors.
        return -1;
    }

    if (!create)
    {
        FreeClusterChain(original.cluster);
    }

    return Sync() ? 0 : -1;
}

namespace
{
struct PermissionRecord
{
    uint32_t tag, parent, slot;
    FilePermissions permissions;
    uint32_t reserved, checksum;
};

static_assert(sizeof(PermissionRecord) == 32, "permission record");

uint32_t RecordChecksum(const PermissionRecord& record)
{
    const auto* bytes = (const uint8_t*)&record;
    uint32_t value = 2166136261;
    for (unsigned i = 0; i < 28; ++i)
    {
        value = (value ^ bytes[i]) * 16777619;
    }

    return value;
}
}

bool FAT32Driver::Permissions(DirEntry& entry, bool write, bool remove)
{
    PermissionRecord records[16];
    int free = -1, found = -1;
    for (unsigned sector = 0; sector < PERMISSION_SECTORS; ++sector)
    {
        if (!device->Read((uint64_t)PartitionStart + PERMISSION_START + sector, records, 1))
        {
            ioError = true;
            return false;
        }

        for (unsigned i = 0; i < 16; ++i)
        {
            const auto& record = records[i];
            if (!record.tag)
            {
                PermissionRecord empty;
                memset(&empty, 0, sizeof(empty));
                if (memcmp((void*)&record, &empty, sizeof(record)))
                {
                    return false;
                }

                if (free < 0)
                {
                    free = sector * 16 + i;
                }
            }
            else if (record.tag != 0x36315048 || record.reserved || record.permissions.mode > 0777 ||
                     record.checksum != RecordChecksum(record))
            {
                return false;
            }
            else if (record.parent == entry.parentCluster && record.slot == entry.offsetInParentCluster)
            {
                if (found >= 0)
                {
                    return false;
                }

                found = sector * 16 + i;
                if (!write)
                {
                    entry.permissions = record.permissions;
                }
            }
        }
    }

    if (!write)
    {
        return found >= 0;
    }

    int index = found >= 0 ? found : free;
    if (index < 0 || (remove && found < 0))
    {
        return false;
    }

    uint64_t sector = (uint64_t)PartitionStart + PERMISSION_START + index / 16;
    if (!device->Read(sector, records, 1))
    {
        ioError = true;
        return false;
    }

    auto& record = records[index % 16];
    memset(&record, 0, sizeof(record));
    if (!remove)
    {
        record.tag = 0x36315048;
        record.parent = entry.parentCluster;
        record.slot = entry.offsetInParentCluster;
        record.permissions = entry.permissions;
        record.checksum = RecordChecksum(record);
    }

    if (!device->Write(sector, records, 1))
    {
        ioError = true;
        return false;
    }

    return true;
}

int FAT32Driver::SetPermissions(const char* path, const FilePermissions& permissions)
{
    DirEntry entry;
    if (permissions.mode > 0777 || OpenFile(path, &entry))
    {
        return -1;
    }

    entry.permissions = permissions;
    return Permissions(entry, true) && Sync() ? 0 : -1;
}
