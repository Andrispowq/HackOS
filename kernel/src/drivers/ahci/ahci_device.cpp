#include "ahci_device.h"
#include "arch/x86_64/paging/page_table_manager.h"

extern PageTableManager KernelDirectory;
extern PageTableManager* CurrentDirectory;

namespace AHCI
{
#define HBA_PORT_DEV_PRESENT 0x3
#define HBA_PORT_IPM_ACTIVE 0x1
#define SATA_SIG_ATAPI 0xEB140101
#define SATA_SIG_ATA 0x00000101
#define SATA_SIG_SEMB 0xC33C0101
#define SATA_SIG_PM 0x96690101

#define HBA_PxCMD_CR 0x8000
#define HBA_PxCMD_FRE 0x0010
#define HBA_PxCMD_ST 0x0001
#define HBA_PxCMD_FR 0x4000

PortType CheckPortType(HBAPort* Port)
{
    uint32_t SataStatus = Port->SataStatus;

    uint8_t InterfacePowerManagement = (SataStatus >> 8) & 0b111;
    uint8_t DeviceDetection = SataStatus & 0b111;

    if (DeviceDetection != HBA_PORT_DEV_PRESENT)
    {
        return PortType::None;
    }

    if (InterfacePowerManagement != HBA_PORT_IPM_ACTIVE)
    {
        return PortType::None;
    }

    switch (Port->Signature)
    {
    case SATA_SIG_ATAPI:
        return PortType::SATAPI;
    case SATA_SIG_ATA:
        return PortType::SATA;
    case SATA_SIG_PM:
        return PortType::PM;
    case SATA_SIG_SEMB:
        return PortType::SEMB;
    default:
        return PortType::None;
    }
}

vector<AHCIDevice*> AHCIDriver::PreparePorts()
{
    vector<AHCIDevice*> devices;

    uint32_t PortsImplemented = ABAR->PortsImplemented;
    for (int i = 0; i < 32; i++)
    {
        if (PortsImplemented & (1 << i))
        {
            PortType portType = CheckPortType(&ABAR->Ports[i]);

            if (portType == PortType::SATA || portType == PortType::SATAPI)
            {
                Ports[PortCount] = new Port();
                Ports[PortCount]->AHCIPortType = portType;
                Ports[PortCount]->HBAPortPtr = &ABAR->Ports[i];
                Ports[PortCount]->PortNumber = PortCount;
                PortCount++;

                AHCIDevice* device = new AHCIDevice(Ports[PortCount - 1]);
                devices.push_back(device);
            }
        }
    }

    return devices;
}

void Port::Configure()
{
    StopCMD();

    void* NewBase = PageFrameAllocator::SharedAllocator()->RequestPage();
    HBAPortPtr->CommandListBase = (uint32_t)(uint64_t)NewBase;
    HBAPortPtr->CommandListBaseUpper = (uint32_t)((uint64_t)NewBase >> 32);
    memset((void*)(HBAPortPtr->CommandListBase), 0, 1024);

    void* FISBase = PageFrameAllocator::SharedAllocator()->RequestPage();
    HBAPortPtr->FISBaseAddress = (uint32_t)(uint64_t)FISBase;
    HBAPortPtr->FISBaseAddressUpper = (uint32_t)((uint64_t)FISBase >> 32);
    memset(FISBase, 0, 256);

    HBACommandHeader* CommandHeader =
        (HBACommandHeader*)((uint64_t)HBAPortPtr->CommandListBase + ((uint64_t)HBAPortPtr->CommandListBaseUpper << 32));

    for (int i = 0; i < 32; i++)
    {
        CommandHeader[i].PRDTLength = 8;

        void* CommandTableAddress = PageFrameAllocator::SharedAllocator()->RequestPage();
        uint64_t Address = (uint64_t)CommandTableAddress + (i << 8);
        CommandHeader[i].CommandTableBaseAddress = (uint32_t)(uint64_t)Address;
        CommandHeader[i].CommandTableBaseAddressUpper = (uint32_t)((uint64_t)Address >> 32);
        memset(CommandTableAddress, 0, 256);
    }

    StartCMD();
}

void Port::StopCMD()
{
    HBAPortPtr->CommandStatus &= ~HBA_PxCMD_ST;
    HBAPortPtr->CommandStatus &= ~HBA_PxCMD_FRE;

    while (true)
    {
        if (HBAPortPtr->CommandStatus & HBA_PxCMD_FR)
        {
            continue;
        }

        if (HBAPortPtr->CommandStatus & HBA_PxCMD_CR)
        {
            continue;
        }

        break;
    }
}

void Port::StartCMD()
{
    while (HBAPortPtr->CommandStatus & HBA_PxCMD_CR)
        ;

    HBAPortPtr->CommandStatus |= HBA_PxCMD_FRE;
    HBAPortPtr->CommandStatus |= HBA_PxCMD_ST;
}

// One physical page per port bounds DMA to contiguous memory. Every data
// command is at most eight sectors; flush commands have no PRDT.
bool Port::Transfer(uint8_t command, uint64_t lba, void* buffer, uint32_t sectors)
{
    if (failed || sectors > 8 || lba >= (1ULL << 48) || sectors > (1ULL << 48) - lba)
    {
        return false;
    }

    if (!tempBuffer)
    {
        tempBuffer = (uint64_t)PageFrameAllocator::SharedAllocator()->RequestPage();
        if (!tempBuffer)
        {
            return false;
        }

        KernelDirectory.MapMemory(tempBuffer, tempBuffer);
    }

    unsigned spin = 0;
    while ((HBAPortPtr->TaskFileData & (ATA_DEV_BUSY | ATA_DEV_DRQ)) || HBAPortPtr->CommandIssue)
    {
        if (++spin == 10000000)
        {
            failed = true;
            return false;
        }
    }

    HBAPortPtr->InterruptStatus = ~0U;
    auto header =
        (HBACommandHeader*)((uint64_t)HBAPortPtr->CommandListBase | (uint64_t)HBAPortPtr->CommandListBaseUpper << 32);
    header->CommandFISLength = sizeof(FIS_REG_H2D) / 4;
    header->Write = command == ATA_CMD_WRITE_DMA_EX;
    header->PRDTLength = sectors ? 1 : 0;
    header->PRDBCount = 0;
    auto table = (HBACommandTable*)((uint64_t)header->CommandTableBaseAddress |
                                    (uint64_t)header->CommandTableBaseAddressUpper << 32);
    memset(table, 0, sizeof(HBACommandTable) + sizeof(HBAPRDTEntry));
    if (sectors)
    {
        if (header->Write)
        {
            memcpy((void*)tempBuffer, buffer, sectors * 512);
        }

        table->PRDTEntry[0].DataBaseAddress = tempBuffer;
        table->PRDTEntry[0].DataBaseAddressUpper = tempBuffer >> 32;
        table->PRDTEntry[0].ByteCount = sectors * 512 - 1;
    }

    auto fis = (FIS_REG_H2D*)table->CommandFIS;
    fis->FISType = FIS_TYPE_REG_H2D;
    fis->CommandControl = 1;
    fis->Command = command;
    fis->LBA0 = lba;
    fis->LBA1 = lba >> 8;
    fis->LBA2 = lba >> 16;
    fis->LBA3 = lba >> 24;
    fis->LBA4 = lba >> 32;
    fis->LBA5 = lba >> 40;
    fis->DeviceRegister = 1 << 6;
    fis->CountLow = sectors;
    asm volatile("" ::: "memory");
    HBAPortPtr->CommandIssue = 1;
    spin = 0;
    while (HBAPortPtr->CommandIssue & 1)
    {
        if (++spin == 10000000)
        {
            // Do not reuse DMA storage while a timed-out command may own it.
            failed = true;
            return false;
        }
    }

    asm volatile("" ::: "memory");
    if ((HBAPortPtr->InterruptStatus & HBA_PxIS_TFES) || (HBAPortPtr->TaskFileData & 0x21))
    {
        return false;
    }

    if (sectors && !header->Write)
    {
        memcpy(buffer, (void*)tempBuffer, sectors * 512);
    }

    return true;
}

bool Port::Read(uint64_t lba, void* buffer, uint32_t sectors)
{
    if (!sectors)
    {
        return false;
    }

    while (sectors)
    {
        uint32_t count = sectors < 8 ? sectors : 8;
        if (!Transfer(ATA_CMD_READ_DMA_EX, lba, buffer, count))
        {
            return false;
        }

        lba += count;
        sectors -= count;
        buffer = (uint8_t*)buffer + count * 512;
    }

    return true;
}

bool Port::Write(uint64_t lba, void* buffer, uint32_t sectors)
{
    if (!sectors)
    {
        return false;
    }

    while (sectors)
    {
        uint32_t count = sectors < 8 ? sectors : 8;
        if (!Transfer(ATA_CMD_WRITE_DMA_EX, lba, buffer, count))
        {
            return false;
        }

        lba += count;
        sectors -= count;
        buffer = (uint8_t*)buffer + count * 512;
    }

    return true;
}

bool Port::Flush()
{
    return Transfer(0xea, 0, nullptr, 0); // ATA FLUSH CACHE EXT
}

AHCIDriver::AHCIDriver(PCI::PCIDeviceHeader* PCIBaseAddress) : PCIBaseAddress(PCIBaseAddress)
{
    kprintf("AHCI Driver instance initialized\n");

    ABAR = (HBAMemory*)((PCI::PCIHeaderType_0*)PCIBaseAddress)->BAR5;
    KernelDirectory.MapMemory((vaddr_t)ABAR, (vaddr_t)ABAR);
}

AHCIDevice::AHCIDevice(Port* port) : port(port)
{
    port->Configure();
    sectors = port->Identify();
}

bool AHCIDevice::Read(uint64_t LBA, void* buffer, uint64_t size)
{
    return buffer && size && LBA < sectors && size <= sectors - LBA && size <= 0xffffffff &&
           port->Read(LBA, buffer, (uint32_t)size);
}

bool AHCIDevice::Write(uint64_t LBA, void* buffer, uint64_t size)
{
    return buffer && size && LBA < sectors && size <= sectors - LBA && size <= 0xffffffff &&
           port->Write(LBA, buffer, (uint32_t)size);
}

bool AHCIDevice::Flush()
{
    return port->Flush();
}
};

uint64_t AHCI::Port::Identify()
{
    uint16_t words[256];
    if (!Transfer(0xec, 0, words, 1) || !(words[83] & (1 << 10)))
    {
        return 0;
    }

    uint64_t size =
        words[100] | ((uint64_t)words[101] << 16) | ((uint64_t)words[102] << 32) | ((uint64_t)words[103] << 48);
    return size < (1ULL << 48) ? size : 0;
}

uint64_t AHCI::AHCIDevice::SectorCount() const
{
    return sectors;
}
