#ifndef BITMAP_H
#define BITMAP_H

#include "stdint.h"

class Bitmap
{
public:
    bool Get(uint64_t index);
    void Set(uint64_t index, bool value);
    uint64_t First();

    void SetSize(uint64_t size) { this->size = size; }
    void SetBuffer(uint64_t* buffer) { this->buffer = buffer; }

public:
    uint64_t size;
    uint64_t* buffer;
};

#endif