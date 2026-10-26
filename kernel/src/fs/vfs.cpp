#include "vfs.h"
#include "filesystem.h"
#include "lib/memory.h"
#include "lib/string.h"

void* fopen(const char* path, int flags)
{
    if (!path || path[0] != 'f' || path[1] != 's') return nullptr;
    size_t position = 2;
    size_t index = 0;
    if (path[position] < '0' || path[position] > '9') return nullptr;
    while (path[position] >= '0' && path[position] <= '9')
    {
        index = index * 10 + path[position++] - '0';
        if (index >= MAX_FILESYSTEMS) return nullptr;
    }
    if (path[position] != ':' || path[position + 1] != '/') return nullptr;
    Filesystem* fs = filesystems[index];
    if (!fs) return nullptr;

    const char* suffix = path + position + 1;
    size_t length = strlen((char*)suffix);
    char* translated = new char[length + 2];
    translated[0] = '~';
    memcpy(translated + 1, suffix, length + 1);
    ActiveFile* file = fs->OpenFile(translated);
    if (!file && (flags & O_CREAT))
    {
        size_t last = length;
        while (last && translated[last] != '/') --last;
        if (translated[last + 1])
        {
            translated[last] = 0;
            file = fs->CreateFile(translated, translated + last + 1, ARCHIVE);
        }
    }
    delete[] translated;
    if (file) file->fs_index = index;
    return file;
}

static Filesystem* FileSystemFor(void* handle)
{
    if (!handle) return nullptr;
    ActiveFile* file = (ActiveFile*)handle;
    if (file->fs_index >= MAX_FILESYSTEMS) return nullptr;
    return filesystems[file->fs_index];
}

int fread(void* buffer, int size, int n, void* file)
{
    Filesystem* fs = FileSystemFor(file);
    if (!fs || size < 0 || n < 0 || (!buffer && size && n)) return -1;
    return fs->Read((ActiveFile*)file, buffer, (uint64_t)size * (uint64_t)n);
}

int fwrite(void* buffer, int size, int n, void* file)
{
    Filesystem* fs = FileSystemFor(file);
    if (!fs || size < 0 || n < 0 || (!buffer && size && n)) return -1;
    return fs->Write((ActiveFile*)file, buffer, (uint64_t)size * (uint64_t)n);
}

void fclose(void* file)
{
    Filesystem* fs = FileSystemFor(file);
    if (fs) fs->CloseFile((ActiveFile*)file);
}
