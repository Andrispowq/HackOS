#include "hardware.h"
#include "hackos/abi.h"
#include "init/kernelInit.h"
#include "acpi/pci/pci.h"
#include "acpi/apic/madt.h"
#include "drivers/device/device.h"
#include "drivers/mouse/mouse.h"
#include "drivers/keyboard/keyboard.h"
#include "lib/memory.h"

extern KernelInfo* kInfo;
extern MemoryMap* map;
extern bool fromUEFI;

static void Text(char* destination, const void* source, unsigned length)
{
    const char* text = (const char*)source;
    for (unsigned i = 0; i < length; ++i)
    {
        destination[i] = text[i] >= 32 && text[i] <= 126 ? text[i] : ' ';
    }
}

static void CPU(HackosHardwareInfo& info)
{
    auto& cpu = info.cpu;
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0), "c"(0));
    uint32_t maximum = a;
    memcpy(cpu.vendor, &b, 4);
    memcpy(cpu.vendor + 4, &d, 4);
    memcpy(cpu.vendor + 8, &c, 4);
    if (maximum >= 1)
    {
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
        cpu.family = (a >> 8) & 15;
        cpu.model = (a >> 4) & 15;
        cpu.stepping = a & 15;
        if (cpu.family == 6 || cpu.family == 15)
        {
            cpu.model |= ((a >> 16) & 15) << 4;
        }
        if (cpu.family == 15)
        {
            cpu.family += (a >> 20) & 255;
        }
        cpu.featuresEcx = c;
        cpu.featuresEdx = d;
    }

    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000), "c"(0));
    maximum = a;
    if (maximum >= 0x80000001)
    {
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001), "c"(0));
        cpu.extendedEdx = d;
    }

    if (maximum >= 0x80000004)
    {
        for (unsigned i = 0; i < 3; ++i)
        {
            asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000002 + i), "c"(0));
            memcpy(cpu.brand + i * 16, &a, 4);
            memcpy(cpu.brand + i * 16 + 4, &b, 4);
            memcpy(cpu.brand + i * 16 + 8, &c, 4);
            memcpy(cpu.brand + i * 16 + 12, &d, 4);
        }
    }

    cpu.discoveredCpus = ACPI::DiscoveredCPUCount();
    cpu.activeCpus = 1;
}

uint64_t ReadHardware(uint64_t section, uint64_t index, HackosHardwareInfo& info)
{
    memset(&info, 0, sizeof(info));
    if (section >= HACKOS_HW_COUNT)
    {
        return HACKOS_ERROR;
    }
    if (section != HACKOS_HW_DISK && section != HACKOS_HW_PCI && section != HACKOS_HW_ACPI &&
        section != HACKOS_HW_MEMORY_MAP && index)
    {
        return 0;
    }

    switch (section)
    {
    case HACKOS_HW_SYSTEM:
        info.system.uefi = fromUEFI;
        info.system.pciAvailable = PCI::InventoryAvailable();
        info.system.pciStored = PCI::InventoryCount();
        info.system.pciDropped = PCI::InventoryDropped();
        info.system.timerHz = TimerFrequency();
        if (kInfo->rsdp)
        {
            info.system.acpiRevision = kInfo->rsdp->Revision;
            Text(info.system.oem, kInfo->rsdp->OEMID, 6);
        }

        break;
    case HACKOS_HW_CPU:
        CPU(info);
        break;
    case HACKOS_HW_MEMORY:
    {
        auto allocator = PageFrameAllocator::SharedAllocator();
        info.memory.freeBytes = allocator->GetFreeRAM();
        info.memory.usedBytes = allocator->GetUsedRAM();
        info.memory.reservedBytes = allocator->GetReservedRAM();
        info.memory.firmwareUsableBytes = map->GetUsuableSystemMemory();
        info.memory.entries = map->GetEntryCount();
        info.memory.pageSize = 4096;
        break;
    }
    case HACKOS_HW_DISK:
    {
        unsigned slot = 0;
        for (; slot < MAX_DEVICE_COUNT; ++slot)
        {
            if (devices[slot])
            {
                if (!index)
                {
                    break;
                }
                --index;
            }
        }

        if (slot == MAX_DEVICE_COUNT)
        {
            return 0;
        }
        info.disk = {devices[slot]->SectorCount(), slot, (uint32_t)devices[slot]->GetType(), 512, slot == 0};
        break;
    }
    case HACKOS_HW_PCI:
        return PCI::InventoryRecord(index, info) ? 1 : 0;
    case HACKOS_HW_PS2:
        info.ps2 = {KeyboardRegistered(), MouseEnabled(), MouseDeviceId()};
        break;
    case HACKOS_HW_DISPLAY:
        info.display = {kInfo->framebuffer.width, kInfo->framebuffer.height, kInfo->framebuffer.bpp,
                        kInfo->framebuffer.pitch};
        break;
    case HACKOS_HW_ACPI:
    {
        if (!kInfo->rsdp || index >= kInfo->rsdp->GetTableCount())
        {
            return 0;
        }
        auto table = kInfo->rsdp->Get(index);
        if (!table)
        {
            return HACKOS_ERROR;
        }
        Text(info.acpi.signature, table->Signature, 4);
        Text(info.acpi.oem, table->OEMID, 6);
        Text(info.acpi.tableId, table->OEMTableID, 8);
        info.acpi.length = table->Length;
        info.acpi.revision = table->Revision;
        info.acpi.oemRevision = table->OEMRevision;
        break;
    }
    case HACKOS_HW_MEMORY_MAP:
    {
        if (index >= map->GetEntryCount())
        {
            return 0;
        }
        auto entry = map->GetEntry(index);
        info.region = {entry->address, entry->size, entry->type, map->IsUsuableMemory(index)};
        break;
    }
    }

    return 1;
}
