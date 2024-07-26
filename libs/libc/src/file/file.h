#ifndef FILE_H
#define FILE_H

#include <stdint.h>

class ActiveFile
{
public:
    ActiveFile() : seek_position(0) {}
    ~ActiveFile() {}

    virtual uint64_t GetSize() const = 0;
    virtual const char* GetName() const = 0;
    virtual const char* GetPath() const = 0;

    virtual uint32_t GetAttributes() const = 0;

public:
    size_t seek_position = 0;
    size_t fs_index = -1;
};

#endif