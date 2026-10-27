#ifndef DEVICE_H
#define DEVICE_H

#include "lib/stdint.h"
#include "memory/heap.h"

#define MAX_DEVICE_COUNT 16

enum class DeviceType
{
    ATA_PIO = 0x1,
    AHCI = 0x2
};

class Device
{
  public:
    Device()
    {
    }

    virtual ~Device() {};

    virtual DeviceType GetType() const = 0;

    // Counts are 512-byte sectors. False means the transfer did not complete.
    virtual uint64_t SectorCount() const = 0;

    virtual bool Read(uint64_t LBA, void* buffer, uint64_t size) = 0;
    virtual bool Write(uint64_t LBA, void* buffer, uint64_t size) = 0;

    virtual bool Flush()
    {
        return false;
    }
};

extern Device* devices[MAX_DEVICE_COUNT];

void InitialiseDevices();
void RegisterDevice(Device* device);
void UnregisterDevice(Device* device); //Doesn't delete the device

#endif
