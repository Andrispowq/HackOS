#ifndef DEVICE_H
#define DEVICE_H

#include "lib/stdint.h"
#include "memory/heap.h"

enum DeviceType
{
    ATA_PIO = 0x1,
    AHCI = 0x2
};

class Device
{
public:
    Device() {}
    virtual ~Device() {};

    virtual DeviceType GetType() = 0;

    virtual void Read(uint64_t LBA, void* buffer, uint64_t size) = 0;
    virtual void Write(uint64_t LBA, void* buffer, uint64_t size) = 0;
};

#endif