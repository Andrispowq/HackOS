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
    Device() {}
    virtual ~Device() {};

    virtual DeviceType GetType() const = 0;

    virtual void Read(uint64_t LBA, void* buffer, uint64_t size) = 0;
    virtual void Write(uint64_t LBA, void* buffer, uint64_t size) = 0;
};

extern Device* devices[MAX_DEVICE_COUNT];

void InitialiseDevices();
void RegisterDevice(Device* device);
void UnregisterDevice(Device* device); //Doesn't delete the device

#endif