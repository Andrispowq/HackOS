#include "pci.h"

#include "libc/memory.h"
#include "cpu/paging/paging.h"
#include "ahci.h"

AHCIDriver* drivers[5];
uint8_t driverCount = 0;

SDTHeader* GetMCFG(RSDP* rsdp)
{
    SDTHeader* rsdt = 0;
    uint32_t divisor = 0;
    if(rsdp->Revision > 0)
    {
        rsdt = (SDTHeader*)rsdp->XSDTAddress;
        divisor = 8;
    }
    else
    {
        rsdt = (SDTHeader*)(uint64_t)rsdp->RSDTAddress;
        divisor = 4;
    }

    uint64_t entryCount = rsdt->Length - sizeof(SDTHeader);
    entryCount /= divisor;

    for(uint64_t i = 0; i < entryCount; i++)
    {
        uint64_t ptr = (uint64_t)rsdt + (i * divisor) + sizeof(SDTHeader);
        SDTHeader* curr = 0;
        if(rsdp->Revision > 0)
        {
            curr = (SDTHeader*)*(uint64_t*)ptr;
        }
        else
        {
            curr = (SDTHeader*)(uint64_t)*(uint32_t*)ptr;
        }

        if(memcmp(curr->Signature, "MCFG", 4) == 0)
        {
            return curr;
        }
    }

    return 0;
}

static void EnumerateBus(uint64_t baseAddress, uint64_t bus);
static void EnumerateDevice(uint64_t busAddress, uint64_t device);
static void EnumerateFunction(uint64_t deviceAddress, uint64_t function);

void EnumeratePCI(RSDP* rsdp)
{
    drivers[0] = 0;
    drivers[1] = 0;
    drivers[2] = 0;
    drivers[3] = 0;
    drivers[4] = 0;

    MCFGHeader* header = (MCFGHeader*)GetMCFG(rsdp);

    if(!header)
    {
        return;
    }

    int entries = (header->header.Length - sizeof(MCFGHeader)) / sizeof(DeviceConfig);
    for(int i = 0; i < entries; i++)
    {
        DeviceConfig* devConf = (DeviceConfig*)((uint64_t)header + sizeof(MCFGHeader) + sizeof(DeviceConfig) * i);
    
        for(uint64_t bus = (uint64_t)devConf->StartBus; bus < (uint64_t)devConf->EndBus; bus++)
        {
            EnumerateBus(devConf->BaseAddress, bus);
        }
    }
}

static void EnumerateBus(uint64_t baseAddress, uint64_t bus)
{
    uint64_t offset = bus << 20;

    uint64_t busAddress = baseAddress + offset;
    MapMemory(busAddress, busAddress);

    PCIDeviceHeader* pciDevHeader = (PCIDeviceHeader*)busAddress;

    if(pciDevHeader->DeviceID == 0) return;
    if(pciDevHeader->DeviceID == 0xFFFF) return;

    for(uint64_t device = 0; device < 32; device++)
    {
        EnumerateDevice(busAddress, device);
    }
}

static void EnumerateDevice(uint64_t busAddress, uint64_t device)
{
    uint64_t offset = device << 15;

    uint64_t deviceAddress = busAddress + offset;
    MapMemory(deviceAddress, deviceAddress);

    PCIDeviceHeader* pciDevHeader = (PCIDeviceHeader*)deviceAddress;

    if(pciDevHeader->DeviceID == 0) return;
    if(pciDevHeader->DeviceID == 0xFFFF) return;

    for(uint64_t function = 0; function < 8; function++)
    {
        EnumerateFunction(deviceAddress, function);
    }
}

static void EnumerateFunction(uint64_t deviceAddress, uint64_t function)
{
    uint64_t offset = function << 12;

    uint64_t functionAddress = deviceAddress + offset;
    MapMemory(functionAddress, functionAddress);

    PCIDeviceHeader* pciDevHeader = (PCIDeviceHeader*)functionAddress;

    if(pciDevHeader->DeviceID == 0) return;
    if(pciDevHeader->DeviceID == 0xFFFF) return;

    switch (pciDevHeader->Class)
    {
    case 0x01: // mass storage controller
        switch (pciDevHeader->Subclass)
        {
        case 0x06: // SATA
            switch (pciDevHeader->Class)
            {
            case 0x01: // AHCI 1.0 device
                //Initialise AHCI 1.0 driver
                drivers[driverCount++] = SetupDriver(pciDevHeader);
            }
        }   
    }
}