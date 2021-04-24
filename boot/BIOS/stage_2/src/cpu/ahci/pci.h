
#ifndef PCI_H
#define PCI_H

#include "libc/stdint.h"

typedef struct rsdp
{
    char Signature[8];
    uint8_t Checksum;
    char OEMID[6];
    uint8_t Revision;
    uint32_t RSDTAddress;

    uint32_t Length;
    uint64_t XSDTAddress;
    uint8_t ExtendedChecksum;
    uint8_t Reserved[3];
} __attribute__((packed)) RSDP;

typedef struct sdt_header
{
    char Signature[4];
    uint32_t Length;
    uint8_t Revision;
    uint8_t Checksum;
    uint8_t OEMID[6];
    uint8_t OEMTableID[8];
    uint32_t OEMRevision;
    uint32_t CreatorID;
    uint32_t CreatorRevision;
} __attribute__((packed)) SDTHeader;

typedef struct mcfg_header
{
    SDTHeader header;
    uint64_t Reserved;
} __attribute__((packed)) MCFGHeader;

typedef struct dev_config
{
    uint64_t BaseAddress;
    uint16_t PCISegGroup;
    uint8_t StartBus;
    uint8_t EndBus;
    uint32_t Reserved;
} __attribute__((packed)) DeviceConfig;

typedef struct pci_dev_header
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
} PCIDeviceHeader;

typedef struct pci_header_0
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
} PCIHeader0;

SDTHeader* GetMCFG(RSDP* rsdp);

void EnumeratePCI(RSDP* rsdp);

#endif