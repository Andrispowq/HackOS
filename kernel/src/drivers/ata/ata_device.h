#ifndef ATA_DEVICE_H
#define ATA_DEVICE_H

#include "drivers/device/device.h"

class ATADevice : public Device
{
  public:
    ATADevice();
    uint64_t SectorCount() const override;

    virtual ~ATADevice()
    {
    }

    DeviceType GetType() const
    {
        return DeviceType::ATA_PIO;
    };

    bool Read(uint64_t LBA, void* buffer, uint64_t size);
    bool Write(uint64_t LBA, void* buffer, uint64_t size);
    bool Flush() override;

  private:
    uint64_t sectors = 0;
};

#endif
