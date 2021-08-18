#include "pci.h"

#include "arch/x86_64/paging/paging.h"
#include "lib/stdio.h"

#include "drivers/ahci/ahci_device.h"

extern PageTableManager KernelDirectory;

namespace PCI
{
    void EnumerateFunction(uint64_t deviceAddress, uint64_t function)
    {
        uint64_t offset = function << 12;

        uint64_t functionAddress = deviceAddress + offset;
        KernelDirectory.MapMemory(functionAddress, functionAddress);

        PCIDeviceHeader* pciDeviceHeader = (PCIDeviceHeader*)functionAddress;

        if (pciDeviceHeader->DeviceID == 0) return;
        if (pciDeviceHeader->DeviceID == 0xFFFF) return;

        switch (pciDeviceHeader->Class)
        {
        case 0x01: // mass storage controller
            switch (pciDeviceHeader->Subclass)
            {
            case 0x06: //Serial ATA 
                switch (pciDeviceHeader->ProgIF)
                {
                case 0x01: //AHCI 1.0 device
                    AHCI::AHCIDriver* driver = new AHCI::AHCIDriver(pciDeviceHeader);
                    vector<AHCI::AHCIDevice*> devices = driver->PreparePorts();

                    for(uint32_t i = 0; i < devices.size(); i++)
                    {
                        RegisterDevice(devices[i]);
                    }
                }
            }
        }
    }

    void EnumerateDevice(uint64_t busAddress, uint64_t device)
    {
        uint64_t offset = device << 15;

        uint64_t deviceAddress = busAddress + offset;
        KernelDirectory.MapMemory(deviceAddress, deviceAddress);

        PCIDeviceHeader* pciDeviceHeader = (PCIDeviceHeader*)deviceAddress;

        if (pciDeviceHeader->DeviceID == 0) return;
        if (pciDeviceHeader->DeviceID == 0xFFFF) return;

        for (uint64_t function = 0; function < 8; function++)
        {
            EnumerateFunction(deviceAddress, function);
        }
    }

    void EnumerateBus(uint64_t baseAddress, uint64_t bus)
    {
        uint64_t offset = bus << 20;

        uint64_t busAddress = baseAddress + offset;
        KernelDirectory.MapMemory(busAddress, busAddress);

        PCIDeviceHeader* pciDeviceHeader = (PCIDeviceHeader*)busAddress;

        if (pciDeviceHeader->DeviceID == 0) return;
        if (pciDeviceHeader->DeviceID == 0xFFFF) return;

        for (uint64_t device = 0; device < 32; device++)
        {
            EnumerateDevice(busAddress, device);
        }
    }

    void EnumeratePCI(ACPI::MCFGHeader* mcfg)
    {
        uint64_t entries = ((mcfg->header.Length) - sizeof(ACPI::MCFGHeader)) / sizeof(ACPI::DeviceConfig);

        for (uint64_t t = 0; t < entries; t++)
        {
            ACPI::DeviceConfig* newDeviceConfig = (ACPI::DeviceConfig*)((uint64_t)mcfg + sizeof(ACPI::MCFGHeader) + (sizeof(ACPI::DeviceConfig) * t));
            for (uint64_t bus = newDeviceConfig->StartBus; bus < newDeviceConfig->EndBus; bus++)
            {
                EnumerateBus(newDeviceConfig->BaseAddress, bus);
            }
        }
    }
};