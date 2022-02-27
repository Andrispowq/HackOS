#include "pci.h"

#include "arch/x86_64/paging/paging.h"
#include "arch/x86_64/ports.h"
#include "lib/stdio.h"

#include "drivers/ahci/ahci_device.h"

#include "drivers/usb/uhci/uhci.h"

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

        /*kprintf("Found function (%s: %s, %s, %s)\n", 
            GetVendorName(pciDeviceHeader->VendorID),
            DeviceClasses[pciDeviceHeader->Class],
            GetSubclassName(pciDeviceHeader->Class, pciDeviceHeader->Subclass), 
            GetProgIFName(pciDeviceHeader->Class, pciDeviceHeader->Subclass, pciDeviceHeader->ProgIF));*/

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

                    break;
                }
            }
        case 0x0C: // serial bus controller
            switch (pciDeviceHeader->Subclass)
            {
            case 0x03: //USB controller
                switch (pciDeviceHeader->ProgIF)
                {
                case 0x00: //UHCI
                    InitialiseUHCI(PCI_MAKE_ID(pciDeviceHeader->Class, pciDeviceHeader->Subclass, pciDeviceHeader->ProgIF),
                        pciDeviceHeader);
                    break;

                case 0x10: //OHCI
                    break;

                case 0x20: //EHCI
                    break;

                case 0x30: //XHCI
                    break;
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

    // Stuff for reading the PCI
    uint8_t Read8(uint32_t id, uint32_t reg)
    {
        uint32_t addr = 0x80000000 | id | (reg & 0xfc);
        __outl(PCI_CONFIG_ADDR, addr);
        return __inb(PCI_CONFIG_DATA + (reg & 0x03));
    }

    uint16_t Read16(uint32_t id, uint32_t reg)
    {
        uint32_t addr = 0x80000000 | id | (reg & 0xfc);
        __outl(PCI_CONFIG_ADDR, addr);
        return __inw(PCI_CONFIG_DATA + (reg & 0x02));
    }

    uint32_t Read32(uint32_t id, uint32_t reg)
    {
        uint32_t addr = 0x80000000 | id | (reg & 0xfc);
        __outl(PCI_CONFIG_ADDR, addr);
        return __inl(PCI_CONFIG_DATA);
    }

    void Write8(uint32_t id, uint32_t reg, uint8_t data)
    {
        uint32_t address = 0x80000000 | id | (reg & 0xfc);
        __outl(PCI_CONFIG_ADDR, address);
        __outb(PCI_CONFIG_DATA + (reg & 0x03), data);
    }

    void Write16(uint32_t id, uint32_t reg, uint16_t data)
    {
        uint32_t address = 0x80000000 | id | (reg & 0xfc);
        __outl(PCI_CONFIG_ADDR, address);
        __outw(PCI_CONFIG_DATA + (reg & 0x02), data);
    }

    void Write32(uint32_t id, uint32_t reg, uint32_t data)
    {
        uint32_t address = 0x80000000 | id | (reg & 0xfc);
        __outl(PCI_CONFIG_ADDR, address);
        __outl(PCI_CONFIG_DATA, data);
    }

    void ReadBar(uint32_t id, uint32_t index, uint32_t* address, uint32_t* mask)
    {
        uint32_t reg = PCI_CONFIG_BAR0 + index * sizeof(uint32_t);

        // Get the address
        *address = Read32(id, reg);

        // Find out size of the bar
        Write32(id, reg, 0xffffffff);
        *mask = Read32(id, reg);

        // Restore adddress
        Write32(id, reg, *address);
    }

    void GetBar(BaseAddressRegister* bar, uint32_t id, uint32_t index)
    {
        // Read pci bar register
        uint32_t addressLow;
        uint32_t maskLow;
        ReadBar(id, index, &addressLow, &maskLow);

        if (addressLow & PCI_BAR_64)
        {
            // 64-bit mmio
            uint32_t addressHigh;
            uint32_t maskHigh;
            ReadBar(id, index + 1, &addressHigh, &maskHigh);

            bar->address = (void*)(((uint64_t)addressHigh << 32) | (addressLow & ~0xf));
            bar->size = ~(((uint64_t)maskHigh << 32) | (maskLow & ~0xf)) + 1;
            bar->flags = addressLow & 0xf;
        }
        else if (addressLow & PCI_BAR_IO)
        {
            // i/o register
            bar->port = (uint16_t)(addressLow & ~0x3);
            bar->size = (uint16_t)(~(maskLow & ~0x3) + 1);
            bar->flags = addressLow & 0x3;
        }
        else
        {
            // 32-bit mmio
            bar->address = (void *)(uint64_t)(addressLow & ~0xf);
            bar->size = ~(maskLow & ~0xf) + 1;
            bar->flags = addressLow & 0xf;
        }
    }
};