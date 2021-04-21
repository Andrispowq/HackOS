#ifndef OPEN_FILE_H
#define OPEN_FILE_H

#include "lib/stdint.h"

class OpenFile
{
public:
    OpenFile(char* name, size_t size)
        : name(name), size(size), seek_position(0) {}
    ~OpenFile();

    size_t GetSeekPosition() const { return seek_position; }
    void Seek(size_t pos) { this->seek_position = pos; }

    size_t GetFileSize() const { return size; }

private:
    char* name;
    size_t size;
    size_t seek_position;
};

#endif