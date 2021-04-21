#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "device/device.h"
#include "openfile.h"

#define MAX_OPEN_FILES 256

class Filesystem
{
public:
    Filesystem(Device* device);
    ~Filesystem();

    virtual int OpenFile(const char* path, OpenFile* file) = 0;
    virtual int CloseFile(OpenFile* file) = 0;

    virtual int CreateFile(const char* path) = 0;
    virtual int ResizeFile(const char* path, size_t newSize) = 0;
    virtual int DeleteFile(const char* path);

    virtual int GetDirectoryEntryCount(const char* path, uint64_t* count) = 0;
    virtual int GetFile(const char* path, uint64_t index, OpenFile* file) = 0;

    virtual int Read(OpenFile* file, void* buffer, uint64_t size) = 0;
    virtual int Write(OpenFile* file, void* buffer, uint64_t size) = 0;

private:
    Device* device;
    OpenFile openFiles[MAX_OPEN_FILES];
};

#endif