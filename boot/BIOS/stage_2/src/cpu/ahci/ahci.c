#include "ahci.h"

#include "libc/memory.h"
#include "cpu/paging/paging.h"

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

PortType CheckPortType(HBAPort* port)
{
    uint32_t sataStatus = port->sataStatus;
    uint8_t interfacePowerManagement = (sataStatus >> 8) & 0b111;
    uint8_t deviceDetection = sataStatus & 0b111;

    if (deviceDetection != HBA_PORT_DEV_PRESENT) return None;
    if (interfacePowerManagement != HBA_PORT_IPM_ACTIVE) return None;

    switch (port->signature)
    {
        case SATA_SIG_ATAPI:
            return SATAPI;
        case SATA_SIG_ATA:
            return SATA;
        case SATA_SIG_PM:
            return PM;
        case SATA_SIG_SEMB:
            return SEMB;
        default:
            None;
    }
}

void ProbePorts(AHCIDriver* driver)
{
    uint32_t portsImplemented = driver->ABAR->portsImplemented;

    for (int i = 0; i < 32; i++)
    {
        if (portsImplemented & (1 << i))
        {
            PortType portType = CheckPortType(&driver->ABAR->ports[i]);
            if (portType == SATA || portType == SATAPI)
            {
                Port* port = (Port*)kmalloc(sizeof(Port));
                memset(port, 0, sizeof(Port));

                driver->ports[driver->portCount] = port;
                driver->ports[driver->portCount]->portType = portType;
                driver->ports[driver->portCount]->hbaPort = &driver->ABAR->ports[i];
                driver->ports[driver->portCount]->portNumber = driver->portCount;
                driver->portCount++;
            }
        }
    }
}

void Configure(Port* port)
{
    StopCMD(port);

    void* newBase = (uint64_t)RequestPage();
    port->hbaPort->commandListBase = (uint32_t)(uint64_t)newBase;
    port->hbaPort->commandListBaseUpper = (uint32_t)((uint64_t)newBase >> 32);
    memset((void*)(port->hbaPort->commandListBase), 0, 1024);

    void* fisBase = (uint64_t)RequestPage();
    port->hbaPort->fisBaseAddress = (uint32_t)(uint64_t)fisBase;
    port->hbaPort->fisBaseAddressUpper = (uint32_t)((uint64_t)fisBase >> 32);
    memset(fisBase, 0, 256);

    HBACommandHeader* cmdHeader = (HBACommandHeader*)((uint64_t)port->hbaPort->commandListBase + ((uint64_t)port->hbaPort->commandListBaseUpper << 32));
    for (int i = 0; i < 32; i++)
    {
        cmdHeader[i].prdtLength = 8;
        void* cmdTableAddress = (uint64_t)RequestPage();
        uint64_t address = (uint64_t)cmdTableAddress + (i << 8);
        cmdHeader[i].commandTableBaseAddress = (uint32_t)(uint64_t)address;
        cmdHeader[i].commandTableBaseAddressUpper = (uint32_t)((uint64_t)address >> 32);
        memset(cmdTableAddress, 0, 256);
    }

    StartCMD(port);
}

void StopCMD(Port* port)
{
    port->hbaPort->cmdSts &= ~HBA_PxCMD_ST;
    port->hbaPort->cmdSts &= ~HBA_PxCMD_FRE;

    while(1)
    {
        if (port->hbaPort->cmdSts & HBA_PxCMD_FR) continue;
        if (port->hbaPort->cmdSts & HBA_PxCMD_CR) continue;
        break;
    }
}
void StartCMD(Port* port)
{
    while (port->hbaPort->cmdSts & HBA_PxCMD_CR);

    port->hbaPort->cmdSts |= HBA_PxCMD_FRE;
    port->hbaPort->cmdSts |= HBA_PxCMD_ST;
}

int Read(Port* port, uint64_t sector, uint32_t sectorCount, void* buffer)
{
    uint32_t sectorL = (uint32_t) sector;
    uint32_t sectorH = (uint32_t) (/*sector >> 32*/sector >> 32);

    port->hbaPort->interruptStatus = (uint32_t)-1; // Clear pending interrupt bits

    HBACommandHeader* cmdHeader = (HBACommandHeader*)port->hbaPort->commandListBase;
    cmdHeader->commandFISLength = sizeof(FIS_REG_H2D) / sizeof(uint32_t); //command FIS size;
    cmdHeader->write = 0; //this is a read
    cmdHeader->prdtLength = 1;

    HBACommandTable* commandTable = (HBACommandTable*)(cmdHeader->commandTableBaseAddress);
    memset(commandTable, 0, sizeof(HBACommandTable) + (cmdHeader->prdtLength - 1) * sizeof(HBAPRDTEntry));

    commandTable->prdtEntry[0].dataBaseAddress = (uint32_t)(uint64_t)buffer;
    commandTable->prdtEntry[0].dataBaseAddressUpper = (uint32_t)((uint64_t)buffer >> 32);
    commandTable->prdtEntry[0].byteCount = (sectorCount << 9)-1; // 512 bytes per sector
    commandTable->prdtEntry[0].interruptOnCompletion = 1;

    FIS_REG_H2D* cmdFIS = (FIS_REG_H2D*)(&commandTable->commandFIS);
    cmdFIS->fisType = FIS_TYPE_REG_H2D;
    cmdFIS->commandControl = 1; // command
    cmdFIS->command = ATA_CMD_READ_DMA_EX;
    cmdFIS->lba0 = (uint8_t)sectorL;
    cmdFIS->lba1 = (uint8_t)(sectorL >> 8);
    cmdFIS->lba2 = (uint8_t)(sectorL >> 16);
    cmdFIS->lba3 = (uint8_t)(sectorL >> 24);
    cmdFIS->lba4 = (uint8_t)sectorH;
    cmdFIS->lba4 = (uint8_t)(sectorH >> 8);
    cmdFIS->deviceRegister = 1 << 6; //LBA mode
    cmdFIS->countLow = sectorCount & 0xFF;
    cmdFIS->countHigh = (sectorCount >> 8) & 0xFF;

    uint64_t spin = 0;
    while ((port->hbaPort->taskFileData & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000)
    {
        spin ++;
    }

    if (spin == 1000000) 
    {
        return 0;
    }

    port->hbaPort->commandIssue = 1;
    while (1)
    {
        if((port->hbaPort->commandIssue == 0)) break;

        if(port->hbaPort->interruptStatus & HBA_PxIS_TFES)
        {
            return 0;
        }
    }

    return 1;
}

AHCIDriver* SetupDriver(PCIDeviceHeader* pciBaseAddress)
{
    AHCIDriver* driver = (AHCIDriver*)kmalloc(sizeof(AHCIDriver));
    memset(driver, 0, sizeof(AHCIDriver));

    driver->pciBaseAddress = pciBaseAddress;
    driver->ABAR = (HBAMemory*)((PCIHeader0*)pciBaseAddress)->BAR5;

    MapMemory((uint64_t)driver->ABAR, (uint64_t)driver->ABAR);
    ProbePorts(driver);
    
    for (int i = 0; i < driver->portCount; i++)
    {
        Port* port = driver->ports[i];

        Configure(port);
    }

    return driver;
}