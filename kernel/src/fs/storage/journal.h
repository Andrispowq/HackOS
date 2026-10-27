#pragma once
#include "drivers/device/device.h"

// Relative FAT reserved sectors: boot/FSInfo 0..7, journal 8..268,
// permission records 272..399. All integers are little endian.
constexpr uint32_t STORAGE_RESERVED = 512;
constexpr uint32_t JOURNAL_LIMIT = 256;
constexpr uint32_t PERMISSION_START = 272;
constexpr uint32_t PERMISSION_SECTORS = 128;

struct JournalHeader
{
    char magic[8];
    uint32_t version, count;
    uint8_t digest[32];
    uint8_t reserved[464];
};

static_assert(sizeof(JournalHeader) == 512, "journal header");

class JournalDevice : public Device
{
  public:
    JournalDevice(Device* device, uint32_t start, uint32_t sectors);
    ~JournalDevice();
    DeviceType GetType() const override;
    uint64_t SectorCount() const override;
    bool Read(uint64_t lba, void* buffer, uint64_t count) override;
    bool Write(uint64_t lba, void* buffer, uint64_t count) override;
    bool Flush() override;
    bool Begin();
    bool Finish(bool success);
    bool Healthy() const;

  private:
    bool Replay();
    bool Header(uint32_t count);
    bool Target(uint64_t lba) const;
    Device* disk;
    uint32_t start, sectors, count = 0;
    uint64_t* addresses = nullptr;
    uint8_t* data = nullptr;
    uint8_t* permissions = nullptr;
    bool healthy = false, active = false;
};
