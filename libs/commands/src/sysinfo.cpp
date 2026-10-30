#include "output.h"

static ReportOutput out;
static bool failed;

static bool Read(uint64_t section, uint64_t index, HackosHardwareInfo& info)
{
    if (!out.ok || failed)
    {
        return false;
    }
    uint64_t status = syscall_hardware(section, index, &info);
    if (status == 1)
    {
        return true;
    }
    if (status != 0)
    {
        Error(status == HACKOS_DENIED ? "sysinfo: root privileges required.\n" : "sysinfo: hardware query failed.\n");
        failed = true;
    }

    return false;
}

static void Value(const char* key, uint64_t value)
{
    out.Field(key);
    out.Number(value);
    out.Text("\n");
}

static void System()
{
    HackosHardwareInfo info;
    if (!Read(HACKOS_HW_SYSTEM, 0, info))
    {
        return;
    }
    out.Text("HackOS system report\n====================\n");
    out.Field("Architecture");
    out.Text("x86-64\n");
    out.Field("Boot firmware");
    out.Text(info.system.uefi ? "UEFI\n" : "BIOS\n");
    out.Field("ACPI OEM");
    out.Text(info.system.oem);
    out.Text("\n");
    Value("Timer rate (nominal Hz)", info.system.timerHz);
}

static void Cpu()
{
    HackosHardwareInfo info;
    if (!Read(HACKOS_HW_CPU, 0, info))
    {
        return;
    }
    auto& cpu = info.cpu;
    out.Heading("CPU");
    out.Field("Vendor");
    out.Text(cpu.vendor);
    out.Text("\n");
    out.Field("Model");
    out.Text(cpu.brand[0] ? cpu.brand : "Unavailable");
    out.Text("\n");
    out.Field("Family / model / step");
    out.Number(cpu.family);
    out.Text(" / ");
    out.Number(cpu.model);
    out.Text(" / ");
    out.Number(cpu.stepping);
    out.Text("\n");
    Value("ACPI CPUs discovered", cpu.discoveredCpus);
    Value("Scheduling CPUs", cpu.activeCpus);
    out.Field("Features");
    if (cpu.featuresEdx & (1U << 0))
    {
        out.Text("FPU ");
    }
    if (cpu.featuresEdx & (1U << 4))
    {
        out.Text("TSC ");
    }
    if (cpu.featuresEdx & (1U << 9))
    {
        out.Text("APIC ");
    }
    if (cpu.featuresEdx & (1U << 25))
    {
        out.Text("SSE ");
    }
    if (cpu.featuresEdx & (1U << 26))
    {
        out.Text("SSE2 ");
    }
    if (cpu.featuresEcx & (1U << 30))
    {
        out.Text("RDRAND ");
    }
    if (cpu.extendedEdx & (1U << 20))
    {
        out.Text("NX ");
    }
    if (cpu.extendedEdx & (1U << 29))
    {
        out.Text("LONG-MODE");
    }
    out.Text("\n");
    out.Field("CPUID masks (hex)");
    out.Number(cpu.featuresEdx, 16, 8);
    out.Text(" / ");
    out.Number(cpu.featuresEcx, 16, 8);
    out.Text(" / ");
    out.Number(cpu.extendedEdx, 16, 8);
    out.Text("\n  Secondary CPUs are discovered; SMP scheduling is not implemented.\n");
}

static void Memory()
{
    HackosHardwareInfo info;
    if (!Read(HACKOS_HW_MEMORY, 0, info))
    {
        return;
    }
    auto& memory = info.memory;
    out.Heading("Memory");
    out.Bytes("Firmware usable RAM", memory.firmwareUsableBytes);
    out.Bytes("Allocated frames", memory.usedBytes);
    out.Bytes("Free frames", memory.freeBytes);
    out.Bytes("Reserved address span", memory.reservedBytes);
    Value("Page size (bytes)", memory.pageSize);
    Value("Firmware map entries", memory.entries);
    out.Text("  Reserved span includes holes; it is not installed RAM. DIMM/SPD data is unavailable.\n");
}

static void MemoryMap()
{
    out.Heading("Firmware memory map");
    out.Format("  %-16s   %-16s   %-4s  %s\n", "Physical start", "Bytes", "Type", "Allocator usable");
    HackosHardwareInfo info;
    for (uint64_t i = 0; Read(HACKOS_HW_MEMORY_MAP, i, info); ++i)
    {
        out.Format("  %016lx   %016lx   %04x  %s\n", info.region.address, info.region.size, info.region.type,
                   info.region.usable ? "yes" : "no");
    }

    out.Text("  Values are hexadecimal; type numbers follow the boot firmware (E820 or EFI).\n");
}

static void Disk()
{
    out.Heading("Disk / block devices");
    HackosHardwareInfo info;
    uint64_t count = 0;
    for (; Read(HACKOS_HW_DISK, count, info); ++count)
    {
        out.Text("  disk");
        out.Number(info.disk.slot);
        out.Text(info.disk.type == 1 ? "  ATA PIO" : info.disk.type == 2 ? "  AHCI" : "  Unknown driver");
        if (info.disk.bootDevice)
        {
            out.Text("  [FAT32 boot device]");
        }
        out.Text("\n");
        Value("512-byte sectors", info.disk.sectors);
        if (info.disk.sectors <= ~0ULL / info.disk.sectorSize)
        {
            out.Bytes("Capacity", info.disk.sectors * info.disk.sectorSize);
        }

        if (!info.disk.sectors)
        {
            out.Text("  Capacity unavailable; device is not ready.\n");
        }
    }

    if (!count)
    {
        out.Text("  No registered block devices.\n");
    }
    out.Text("  Registry entries may refer to the same disk through different drivers.\n");
}

static const char* PciClass(uint8_t code)
{
    const char* names[] = {"Unclassified", "Storage",    "Network",           "Display",           "Multimedia",
                           "Memory",       "Bridge",     "Communication",     "System peripheral", "Input",
                           "Docking",      "Processor",  "Serial bus",        "Wireless",          "Intelligent I/O",
                           "Satellite",    "Encryption", "Signal processing", "Accelerator",       "Instrumentation"};
    return code < sizeof(names) / sizeof(*names) ? names[code] : "Unknown class";
}

static const char* UsbInterface(uint8_t code)
{
    switch (code)
    {
    case 0:
        return "UHCI (experimental initialization)";
    case 0x10:
        return "OHCI (driver not implemented)";
    case 0x20:
        return "EHCI (driver not implemented)";
    case 0x30:
        return "XHCI (driver not implemented)";
    default:
        return "Unknown USB interface";
    }
}

static void Pci(bool usb)
{
    HackosHardwareInfo info;
    if (!Read(HACKOS_HW_SYSTEM, 0, info))
    {
        return;
    }
    out.Heading(usb ? "USB controllers" : "PCI devices");
    if (!info.system.pciAvailable)
    {
        out.Text("  PCI inventory unavailable (no usable MCFG discovery).\n");
        return;
    }

    uint32_t dropped = info.system.pciDropped;
    out.Format("  %-12s  %-11s  %-12s %-3s  %s\n", "Address", "Vendor:Dev", "Class:Sub:IF", "Rev", "Description");
    uint64_t count = 0;
    for (uint64_t i = 0; Read(HACKOS_HW_PCI, i, info); ++i)
    {
        auto& pci = info.pci;
        if (usb && (pci.classCode != 12 || pci.subclass != 3))
        {
            continue;
        }
        ++count;
        out.Format("  %04x:%02x:%02x.%u  %04x:%04x    %02x:%02x:%02x     %02x   ", pci.segment, pci.bus, pci.slot,
                   pci.function, pci.vendor, pci.device, pci.classCode, pci.subclass, pci.interface, pci.revision);
        out.Text(usb ? UsbInterface(pci.interface) : PciClass(pci.classCode));
        if (!usb && pci.classCode == 1 && pci.subclass == 6 && pci.interface == 1)
        {
            out.Text(" (AHCI)");
        }
        out.Text("\n");
    }

    Value(usb ? "USB controllers listed" : "PCI functions listed", count);
    if (dropped)
    {
        Value("Omitted PCI functions", dropped);
        out.Text("  Inventory is incomplete: the 512-function limit was reached.\n");
    }

    if (usb)
    {
        out.Text("  Attached USB devices are not enumerated by a complete USB stack.\n");
    }
}

static void Ps2()
{
    HackosHardwareInfo info;
    if (!Read(HACKOS_HW_PS2, 0, info))
    {
        return;
    }
    out.Heading("PS/2 input");
    out.Field("Keyboard");
    out.Text(info.ps2.keyboardRegistered ? "IRQ1 registered; translated set 1; US ASCII\n" : "Not registered\n");
    out.Field("Mouse");
    out.Text(info.ps2.mouseEnabled ? "Enabled; IRQ12; three buttons\n" : "Enable command failed / unavailable\n");
    out.Field("Mouse protocol");
    out.Text(info.ps2.mouseId == 3 ? "IntelliMouse ID 3; four-byte packets; wheel\n"
                                   : "Basic ID 0; three-byte packets\n");
    out.Text("  Keyboard registration is not a hardware presence test.\n");
}

static void Display()
{
    HackosHardwareInfo info;
    if (!Read(HACKOS_HW_DISPLAY, 0, info))
    {
        return;
    }
    out.Heading("Display");
    out.Field("Framebuffer");
    out.Number(info.display.width);
    out.Text(" x ");
    out.Number(info.display.height);
    out.Text(" x ");
    out.Number(info.display.bitsPerPixel);
    out.Text(" bpp\n");
    Value("Scanline pitch (pixels)", info.display.pitch);
    out.Text("  Software framebuffer compositor; no accelerated GPU driver.\n");
}

static void Acpi()
{
    out.Heading("ACPI tables");
    out.Format("  %-8s %8s %10s  %-8s %-12s %s\n", "Table", "Revision", "Bytes", "OEM", "Table ID", "OEM revision");
    HackosHardwareInfo info;
    uint64_t count = 0;
    for (; Read(HACKOS_HW_ACPI, count, info); ++count)
    {
        out.Format("  %-8s %8u %10u  %-8s %-12s %u\n", info.acpi.signature, info.acpi.revision, info.acpi.length,
                   info.acpi.oem, info.acpi.tableId, info.acpi.oemRevision);
    }

    if (!count)
    {
        out.Text("  No ACPI tables available.\n");
    }
    out.Text("  Table presence does not imply an active driver (for example HPET).\n");
}

static void Address(const char* label, const uint8_t* bytes, unsigned count, unsigned base, const char* separator)
{
    out.Field(label);
    for (unsigned i = 0; i < count; ++i)
    {
        if (i) out.Text(separator);
        out.Number(bytes[i], base, base == 16 ? 2 : 0);
    }
    out.Text("\n");
}

static void Network()
{
    HackosHardwareInfo info;
    if (!Read(HACKOS_HW_NETWORK, 0, info))
    {
        return;
    }
    auto& network = info.network;
    out.Heading("Network / Ethernet demo");
    if (!network.available)
    {
        out.Text("  No supported NIC. Enable E1000 with the QEMU runner's --network option.\n");
        return;
    }
    out.Field("Driver");
    out.Text("Intel E1000 (82540EM)\n");
    out.Field("Link");
    out.Text(network.linkUp ? "up\n" : "down\n");
    Address("MAC address", network.mac, 6, 16, ":");
    Address("Static IPv4 address", network.address, 4, 10, ".");
    Address("Gateway", network.gateway, 4, 10, ".");
    out.Field("Gateway ARP");
    out.Text(network.arpState == HACKOS_ARP_RESOLVED ? "resolved\n" :
             network.arpState == HACKOS_ARP_TIMEOUT ? "timed out\n" : "waiting\n");
    if (network.arpState == HACKOS_ARP_RESOLVED)
    {
        Address("Gateway MAC", network.gatewayMac, 6, 16, ":");
    }
    Value("ARP attempts", network.attempts);
    Value("TX frames queued", network.txFrames);
    Value("RX frames received", network.rxFrames);
    Value("TX drops", network.txDropped);
    Value("RX descriptor drops", network.rxDropped);
    Value("ARP replies queued", network.arpReplies);
    out.Text("  Ethernet + ARP + IPv4/ICMP. Try ping 10.0.2.2. TCP/DNS are not implemented.\n");
}

int main(int argc, char** argv)
{
    const char* section = argc == 1 ? "full" : argv[1];
    bool help = argc == 2 && (Equal(section, "--help") || Equal(section, "-h"));
    const char* usage = "Usage: sysinfo [full|system|cpu|memory|memory-map|disk|pci|usb|ps2|display|acpi|network]\n"
                        "Root only. Default: full. ps/2 and pc/2 are aliases for ps2.\n"
                        "Save a report: sysinfo full > /tmp/hardware.txt\n";
    const char* names[] = {"full", "system", "cpu",     "memory", "memory-map", "disk", "pci",
                           "usb",  "ps2",    "display", "acpi",   "ps/2",       "pc/2", "network"};
    unsigned selected = sizeof(names) / sizeof(*names);
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i)
    {
        if (Equal(section, names[i]))
        {
            selected = i;
        }
    }

    if (help)
    {
        Print(usage);
        return 0;
    }

    if (argc > 2 || selected == sizeof(names) / sizeof(*names))
    {
        Error(usage);
        return 2;
    }

    // The kernel authorizes every query, including direct calls by other programs.
    HackosHardwareInfo info;
    if (!Read(HACKOS_HW_SYSTEM, 0, info))
    {
        return 1;
    }
    bool full = selected == 0;
    if (full || selected == 1)
    {
        System();
    }
    if (full || selected == 2)
    {
        Cpu();
    }
    if (full || selected == 3)
    {
        Memory();
    }
    if (full || selected == 4)
    {
        MemoryMap();
    }
    if (full || selected == 5)
    {
        Disk();
    }
    if (full || selected == 6)
    {
        Pci(false);
    }
    if (full || selected == 7)
    {
        Pci(true);
    }
    if (full || selected == 8 || selected == 11 || selected == 12)
    {
        Ps2();
    }
    if (full || selected == 9)
    {
        Display();
    }
    if (full || selected == 10)
    {
        Acpi();
    }
    if (full || selected == 13)
    {
        Network();
    }
    return out.ok && !failed ? 0 : 1;
}
