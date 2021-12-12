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
            return PortType::None;
        if (InterfacePowerManagement != HBA_PORT_IPM_ACTIVE) 
            return PortType::None;

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
            PortType::None;
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

        HBACommandHeader* CommandHeader = (HBACommandHeader*)((uint64_t)HBAPortPtr->CommandListBase + ((uint64_t)HBAPortPtr->CommandListBaseUpper << 32));

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

        while(true)
        {
            if (HBAPortPtr->CommandStatus & HBA_PxCMD_FR) 
                continue;
            if (HBAPortPtr->CommandStatus & HBA_PxCMD_CR) 
                continue;

            break;
        }

    }

    void Port::StartCMD()
    {
        while (HBAPortPtr->CommandStatus & HBA_PxCMD_CR);

        HBAPortPtr->CommandStatus |= HBA_PxCMD_FRE;
        HBAPortPtr->CommandStatus |= HBA_PxCMD_ST;
    }

    //THIS IS PROBLEMATIC!!!!!!!!!!!
    bool Port::Read(uint64_t Sector, void* Buffer, uint32_t SectorCount)
    {
        HBAPortPtr->InterruptStatus = (uint32_t) -1; // Clear pending interrupt bits

        HBACommandHeader* CommandHeader = (HBACommandHeader*)HBAPortPtr->CommandListBase;
        CommandHeader->CommandFISLength = sizeof(FIS_REG_H2D) / sizeof(uint32_t); //command FIS size;
        CommandHeader->Write = 0; //this is a read
        CommandHeader->PRDTLength = 1;

        HBACommandTable* CommandTable = (HBACommandTable*)(CommandHeader->CommandTableBaseAddress);
        memset(CommandTable, 0, sizeof(HBACommandTable) + (CommandHeader->PRDTLength - 1) * sizeof(HBAPRDTEntry));

        tempBuffer = (uint64_t)0x200000000000;
        for(uint32_t i = 0; i < (SectorCount / 8) + 1; i++)
        {
            KernelDirectory.MapMemory(tempBuffer + i * 0x1000,
                (uint64_t)PageFrameAllocator::SharedAllocator()->RequestPage());
        }

        uint64_t physTempBuffer = KernelDirectory.PhysicalAddress(tempBuffer);

        CommandTable->PRDTEntry[0].DataBaseAddress = (uint32_t)physTempBuffer;
        CommandTable->PRDTEntry[0].DataBaseAddressUpper = (uint32_t)(physTempBuffer >> 32);
        CommandTable->PRDTEntry[0].ByteCount = (SectorCount << 9) - 1; // 512 bytes per sector
        CommandTable->PRDTEntry[0].InterruptOnCompletion = 1;

        FIS_REG_H2D* CommandFIS = (FIS_REG_H2D*)(&CommandTable->CommandFIS);

        CommandFIS->FISType = FIS_TYPE_REG_H2D;
        CommandFIS->CommandControl = 1; // command
        CommandFIS->Command = ATA_CMD_READ_DMA_EX;

        CommandFIS->LBA0 = (uint8_t)(Sector);
        CommandFIS->LBA1 = (uint8_t)(Sector >> 8);
        CommandFIS->LBA2 = (uint8_t)(Sector >> 16);
        CommandFIS->LBA3 = (uint8_t)(Sector >> 24);
        CommandFIS->LBA4 = (uint8_t)(Sector >> 32);
        CommandFIS->LBA5 = (uint8_t)(Sector >> 40);

        CommandFIS->DeviceRegister = 1 << 6; //LBA mode

        CommandFIS->CountLow = SectorCount & 0xFF;
        CommandFIS->CountHigh = (SectorCount >> 8) & 0xFF;

        uint64_t Spin = 0;

        while ((HBAPortPtr->TaskFileData & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && Spin < 1000000)
        {
            Spin++;
        }

        if (Spin == 1000000) 
        {
            return false;
        }

        HBAPortPtr->CommandIssue = 1;

        while (true)
        {
            if((HBAPortPtr->CommandIssue == 0)) 
                break;
            
            if(HBAPortPtr->InterruptStatus & HBA_PxIS_TFES)
            {
                return false;
            }
        }

        memcpy(Buffer, (void*)tempBuffer, SectorCount * 512);
        PageFrameAllocator::SharedAllocator()->FreePages((void*)tempBuffer, SectorCount / 8);

        return true;
    }

    bool Port::Write(uint64_t Sector, void* Buffer, uint32_t SectorCount)
    {
        HBAPortPtr->InterruptStatus = (uint32_t) -1; // Clear pending interrupt bits

        HBACommandHeader* CommandHeader = (HBACommandHeader*)HBAPortPtr->CommandListBase;
        CommandHeader->CommandFISLength = sizeof(FIS_REG_H2D) / sizeof(uint32_t); //command FIS size;
        CommandHeader->Write = 1; //this is a write
        CommandHeader->PRDTLength = 1;

        HBACommandTable* CommandTable = (HBACommandTable*)(CommandHeader->CommandTableBaseAddress);
        memset(CommandTable, 0, sizeof(HBACommandTable) + (CommandHeader->PRDTLength - 1) * sizeof(HBAPRDTEntry));

        uint64_t buffAddress = (uint64_t)CurrentDirectory->PhysicalAddress((uint64_t)Buffer) + ((uint64_t)Buffer & 0xFFF);

        CommandTable->PRDTEntry[0].DataBaseAddress = (uint32_t)buffAddress;
        CommandTable->PRDTEntry[0].DataBaseAddressUpper = (uint32_t)(buffAddress >> 32);
        CommandTable->PRDTEntry[0].ByteCount = (SectorCount << 9) - 1; // 512 bytes per sector
        CommandTable->PRDTEntry[0].InterruptOnCompletion = 1;

        FIS_REG_H2D* CommandFIS = (FIS_REG_H2D*)(&CommandTable->CommandFIS);

        CommandFIS->FISType = FIS_TYPE_REG_H2D;
        CommandFIS->CommandControl = 1; // command
        CommandFIS->Command = ATA_CMD_READ_DMA_EX;

        CommandFIS->LBA0 = (uint8_t)(Sector);
        CommandFIS->LBA1 = (uint8_t)(Sector >> 8);
        CommandFIS->LBA2 = (uint8_t)(Sector >> 16);
        CommandFIS->LBA3 = (uint8_t)(Sector >> 24);
        CommandFIS->LBA4 = (uint8_t)(Sector >> 32);
        CommandFIS->LBA5 = (uint8_t)(Sector >> 40);

        CommandFIS->DeviceRegister = 1 << 6; //LBA mode

        CommandFIS->CountLow = SectorCount & 0xFF;
        CommandFIS->CountHigh = (SectorCount >> 8) & 0xFF;

        uint64_t Spin = 0;

        while ((HBAPortPtr->TaskFileData & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && Spin < 1000000)
        {
            Spin++;
        }

        if (Spin == 1000000) 
        {
            return false;
        }

        HBAPortPtr->CommandIssue = 1;

        while (true)
        {
            if((HBAPortPtr->CommandIssue == 0)) 
                break;
            
            if(HBAPortPtr->InterruptStatus & HBA_PxIS_TFES)
            {
                return false;
            }
        }

        return true;
    }

    AHCIDriver::AHCIDriver(PCI::PCIDeviceHeader* PCIBaseAddress)
        : PCIBaseAddress(PCIBaseAddress)
    {
        kprintf("AHCI Driver instance initialized\n");

        ABAR = (HBAMemory*)((PCI::PCIHeaderType_0*)PCIBaseAddress)->BAR5;
        KernelDirectory.MapMemory((vaddr_t)ABAR, (vaddr_t)ABAR);
    }

    AHCIDevice::AHCIDevice(Port* port)
        : port(port)
    {
        port->Configure();
    }    

    void AHCIDevice::Read(uint64_t LBA, void* buffer, uint64_t size)
    {
        port->Read(LBA, buffer, (uint32_t)size);
    }

    void AHCIDevice::Write(uint64_t LBA, void* buffer, uint64_t size)
    {
        port->Write(LBA, buffer, (uint32_t)size);
    }
};