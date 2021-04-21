#ifndef ATA_DEVICE_H
#define ATA_DEVICE_H

#include "drivers/device/device.h"

class ATADevice : public Device
{
public:
    ATADevice() {}
    ~ATADevice() {}

    virtual DeviceType GetType() { return ATA_PIO; };

    virtual void Read(uint64_t LBA, void* buffer, uint64_t size);
    virtual void Write(uint64_t LBA, void* buffer, uint64_t size);
};

#endif