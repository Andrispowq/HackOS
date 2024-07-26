#include "vfs.h"
#include "filesystem.h"

#include "lib/memory.h"
#include "lib/string.h"

extern Filesystem* filesystems[MAX_FILESYSTEMS];

void* fopen(const char* path, int flags)
{
    char fs_name[6]; //fs12:/USR/BIN/... where fs12: is the important part
    char* to_search = nullptr;

    int fs_index = -1;
    size_t path_length = strlen((char*)path);
    size_t to_search_length = 0;
    for(size_t i = 0; i < path_length; i++)
    {
        if(path[i] == '/')
        {
            memset(fs_name, 0, 6);
            memcpy(fs_name, (const void*)path, i);

            fs_index = (char)fs_name[2] - '0';
            if(i > 4) //2 digits 
            {
                fs_index += ((char)fs_name[3] - '0') * 10;
            }

            to_search_length = path_length - i + 2;
            to_search = new char[to_search_length + 1];
            memset(to_search, 0, to_search_length + 1);
            to_search[0] = '~';
            memcpy(&to_search[1], &path[i], to_search_length - 1);
            break;
        }
    }

    if(fs_index == -1)
    {
        return nullptr;
    }

    Filesystem* fs = filesystems[fs_index];
    if(fs == nullptr)
    {
        return nullptr;
    }

    ActiveFile* file = fs->OpenFile(to_search);
    if((file == nullptr) && ((flags & O_CREAT) == O_CREAT))
    {
        int last_index = -1;
        for(size_t i = 0; i < to_search_length; i++)
        {
            if(to_search[i] == '/')
            {
                last_index = i;
            }
        }

        char* path = new char[last_index + 1];
        memset(path, 0, last_index + 1);
        memcpy(path, to_search, last_index);
        
        file = fs->CreateFile(path, &to_search[last_index + 1], ARCHIVE);
    }

    file->fs_index = fs_index;
    return (void*)file;
}

int fread(void* buffer, int size, int n, void* file)
{
    ActiveFile* _file = (ActiveFile*)file;
    int fs_index = _file->fs_index;
    if(fs_index == -1)
    {
        return -1;
    }

    Filesystem* fs = filesystems[fs_index];
    if(fs == nullptr)
    {
        return -1;
    }

    int ret = fs->Read(_file, buffer, size * n);
    return ret;
}

int fwrite(void* buffer, int size, int n, void* file)
{
    ActiveFile* _file = (ActiveFile*)file;
    int fs_index = _file->fs_index;
    if(fs_index == -1)
    {
        return -1;
    }

    Filesystem* fs = filesystems[fs_index];
    if(fs == nullptr)
    {
        return -1;
    }

    int ret = fs->Write(_file, buffer, size * n);
    return ret;
}

void fclose(void* file)
{
    ActiveFile* _file = (ActiveFile*)file;
    int fs_index = _file->fs_index;
    if(fs_index == -1)
    {
        return;
    }

    Filesystem* fs = filesystems[fs_index];
    if(fs == nullptr)
    {
        return;
    }

    fs->CloseFile(_file);
}