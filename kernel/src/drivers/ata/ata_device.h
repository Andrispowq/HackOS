#ifndef ATA_DEVICE_H
#define ATA_DEVICE_H

#include "drivers/device/device.h"

class ATADevice : public Device
{
public:
    ATADevice() {}
    virtual ~ATADevice() {}

    DeviceType GetType() { return DeviceType::ATA_PIO; };

    void Read(uint64_t LBA, void* buffer, uint64_t size);
    void Write(uint64_t LBA, void* buffer, uint64_t size);
};

#endif