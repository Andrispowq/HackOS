#ifndef PCI_H
#define PCI_H

#include "lib/stdint.h"
#include "acpi/system_table.h"

// Useful thing
#define PCI_MAKE_ID(bus, dev, func)     ((bus) << 16) | ((dev) << 11) | ((func) << 8)

// I/O Ports
#define PCI_CONFIG_ADDR                 0xcf8
#define PCI_CONFIG_DATA                 0xcfC

// Header Type
#define PCI_TYPE_MULTIFUNC              0x80
#define PCI_TYPE_GENERIC                0x00
#define PCI_TYPE_PCI_BRIDGE             0x01
#define PCI_TYPE_CARDBUS_BRIDGE         0x02

// PCI Configuration Registers
#define PCI_CONFIG_VENDOR_ID            0x00
#define PCI_CONFIG_DEVICE_ID            0x02
#define PCI_CONFIG_COMMAND              0x04
#define PCI_CONFIG_STATUS               0x06
#define PCI_CONFIG_REVISION_ID          0x08
#define PCI_CONFIG_PROG_INTF            0x09
#define PCI_CONFIG_SUBCLASS             0x0a
#define PCI_CONFIG_CLASS_CODE           0x0b
#define PCI_CONFIG_CACHELINE_SIZE       0x0c
#define PCI_CONFIG_LATENCY              0x0d
#define PCI_CONFIG_HEADER_TYPE          0x0e
#define PCI_CONFIG_BIST                 0x0f

// Type 0x00 (Generic) Configuration Registers
#define PCI_CONFIG_BAR0                 0x10
#define PCI_CONFIG_BAR1                 0x14
#define PCI_CONFIG_BAR2                 0x18
#define PCI_CONFIG_BAR3                 0x1c
#define PCI_CONFIG_BAR4                 0x20
#define PCI_CONFIG_BAR5                 0x24
#define PCI_CONFIG_CARDBUS_CIS          0x28
#define PCI_CONFIG_SUBSYSTEM_VENDOR_ID  0x2c
#define PCI_CONFIG_SUBSYSTEM_DEVICE_ID  0x2e
#define PCI_CONFIG_EXPANSION_ROM        0x30
#define PCI_CONFIG_CAPABILITIES         0x34
#define PCI_CONFIG_INTERRUPT_LINE       0x3c
#define PCI_CONFIG_INTERRUPT_PIN        0x3d
#define PCI_CONFIG_MIN_GRANT            0x3e
#define PCI_CONFIG_MAX_LATENCY          0x3f

// PCI BAR

#define PCI_BAR_IO                      0x01
#define PCI_BAR_LOWMEM                  0x02
#define PCI_BAR_64                      0x04
#define PCI_BAR_PREFETCH                0x08

namespace ACPI
{
    struct MCFGHeader
    {
        SDTHeader header;
        uint64_t reserved;
    } __attribute__((packed));

    struct DeviceConfig
    {
        uint64_t BaseAddress;
        uint16_t PCISegGroup;
        uint8_t StartBus;
        uint8_t EndBus;
        uint32_t Reserved;
    } __attribute__((packed));
};

namespace PCI
{
    struct PCIDeviceHeader
    {
        uint16_t VendorID;
        uint16_t DeviceID;
        uint16_t Command;
        uint16_t Status;
        uint8_t RevisionID;
        uint8_t ProgIF;
        uint8_t Subclass;
        uint8_t Class;
        uint8_t CacheLineSize;
        uint8_t LatencyTimer;
        uint8_t HeaderType;
        uint8_t BIST;
    };

    struct PCIHeaderType_0 
    {
        PCIDeviceHeader Header;

        uint32_t BAR0;
        uint32_t BAR1;
        uint32_t BAR2;
        uint32_t BAR3;
        uint32_t BAR4;
        uint32_t BAR5;

        uint32_t CardbusCISPtr;
        uint16_t SubsystemVendorID;
        uint16_t SubsystemID;

        uint32_t ExpansionROMBaseAddr;
        uint8_t CapabilitiesPtr;

        uint8_t Rsv0;
        uint16_t Rsv1;
        uint32_t Rsv2;

        uint8_t InterruptLine;
        uint8_t InterruptPin;

        uint8_t MinGrant;
        uint8_t MaxLatency;
    };

    struct BaseAddressRegister
    {
        union
        {
            void* address;
            uint16_t port;
        };

        uint64_t size;
        uint32_t flags;
    };

    void EnumeratePCI(ACPI::MCFGHeader* mcfg);

    uint8_t Read8(uint32_t id, uint32_t reg);
    uint16_t Read16(uint32_t id, uint32_t reg);
    uint32_t Read32(uint32_t id, uint32_t reg);

    void Write8(uint32_t id, uint32_t reg, uint8_t data);
    void Write16(uint32_t id, uint32_t reg, uint16_t data);
    void Write32(uint32_t id, uint32_t reg, uint32_t data);

    void ReadBar(uint32_t id, uint32_t index, uint32_t* address, uint32_t* mask);
    void GetBar(PCI::BaseAddressRegister *bar, uint32_t id, uint32_t index);

    //Less useful stuff
    extern const char* DeviceClasses[];

    const char* GetVendorName(uint16_t vendorID);
    const char* GetDeviceName(uint16_t vendorID, uint16_t deviceID);
    const char* GetSubclassName(uint8_t classCode, uint8_t subclassCode);
    const char* GetProgIFName(uint8_t classCode, uint8_t subclassCode, uint8_t progIF);
};

#endif