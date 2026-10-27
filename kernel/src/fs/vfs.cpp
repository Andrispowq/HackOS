#include "vfs.h"
#include "filesystem.h"
#include "lib/memory.h"
#include "lib/string.h"
#include "hackos/runtime/path.h"

namespace
{
struct VfsHandle
{
    ActiveFile* file = nullptr;
    Filesystem* filesystem = nullptr;
    uint64_t token = 0;
    int mode = 0;
};

VfsHandle handles[64];
uint64_t nextToken = 0;

VfsHandle* Find(void* token)
{
    for (auto& handle : handles)
    {
        if (handle.file && handle.token == (uint64_t)token && handle.file->fs_index < MAX_FILESYSTEMS &&
            filesystems[handle.file->fs_index] == handle.filesystem)
        {
            return &handle;
        }
    }

    return nullptr;
}
}

void* fopen(const char* path, int flags)
{
    if (!(flags & (O_READ | O_WRITE)) || (flags & ~(O_READ | O_WRITE | O_CREAT)) ||
        ((flags & O_CREAT) && !(flags & O_WRITE)) || !path || path[0] != 'f' || path[1] != 's')
    {
        return nullptr;
    }

    size_t position = 2;
    size_t index = 0;
    if (path[position] < '0' || path[position] > '9')
    {
        return nullptr;
    }
    while (path[position] >= '0' && path[position] <= '9')
    {
        index = index * 10 + path[position++] - '0';
        if (index >= MAX_FILESYSTEMS)
        {
            return nullptr;
        }
    }

    if (path[position] != ':' || path[position + 1] != '/')
    {
        return nullptr;
    }

    Filesystem* fs = filesystems[index];
    if (!fs)
    {
        return nullptr;
    }

    VfsHandle* available = nullptr;
    for (auto& handle : handles)
    {
        if (!handle.file)
        {
            available = &handle;
            break;
        }
    }

    if (!available || nextToken == ~0ULL)
    {
        return nullptr;
    }

    const char* suffix = path + position + 1;
    char translated[256];
    if (!NormalizePath(suffix, "~", translated))
    {
        return nullptr;
    }

    size_t length = strlen(translated) - 1;
    for (const auto& handle : handles)
    {
        if (handle.file && handle.filesystem == fs && !strcmp((char*)handle.file->GetPath(), translated) &&
            ((handle.mode | flags) & O_WRITE))
        {
            return nullptr;
        }
    }

    ActiveFile* file = nullptr;
    int status = fs->Lookup(translated, file);
    if (status == -2 && (flags & O_CREAT))
    {
        size_t last = length;
        while (last && translated[last] != '/')
        {
            --last;
        }

        if (translated[last + 1])
        {
            translated[last] = 0;
            file = fs->CreateFile(translated, translated + last + 1, ARCHIVE);
        }
    }

    if (file)
    {
        file->fs_index = index;
        file->accessMode = flags;
        if ((file->GetAttributes() & DIRECTORY) || ((flags & O_WRITE) && (file->GetAttributes() & READ_ONLY)))
        {
            fs->CloseFile(file);
            return nullptr;
        }
    }

    if (!file)
    {
        return nullptr;
    }

    *available = {file, fs, ++nextToken, flags};
    return (void*)available->token;
}

int fread(void* buffer, int size, int n, void* token)
{
    auto handle = Find(token);
    if (!handle || !(handle->mode & O_READ) || size < 0 || n < 0 || (!buffer && size && n))
    {
        return -1;
    }

    return handle->filesystem->Read(handle->file, buffer, (uint64_t)size * (uint64_t)n);
}

int fwrite(void* buffer, int size, int n, void* token)
{
    auto handle = Find(token);
    if (!handle || !(handle->mode & O_WRITE) || size < 0 || n < 0 || (!buffer && size && n))
    {
        return -1;
    }

    return handle->filesystem->Write(handle->file, buffer, (uint64_t)size * (uint64_t)n);
}

int fclose(void* token)
{
    auto handle = Find(token);
    if (!handle)
    {
        return -1;
    }

    int result = handle->mode & O_WRITE ? handle->filesystem->Flush() : 0;
    handle->filesystem->CloseFile(handle->file);
    *handle = {};
    return result;
}

int fflush(void* token)
{
    auto handle = Find(token);
    return handle ? handle->filesystem->Flush() : -1;
}
