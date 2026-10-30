#include "hackos/user.h"

int main()
{
    HackosCredentials credentials = {};
    if (syscall_identity(&credentials))
    {
        return 1;
    }
    HackosHardwareInfo info;
    bool ok = true;
    if (credentials.uid != 0)
    {
        for (uint64_t section = 0; section <= HACKOS_HW_COUNT; ++section)
        {
            ok = syscall_hardware(section, 0, &info) == HACKOS_DENIED && ok;
            ok = syscall_hardware(section, ~0ULL, (HackosHardwareInfo*)0x100000) == HACKOS_DENIED && ok;
        }

        User::Print(ok ? "UTILITIES_DENIED_OK\n" : "UTILITIES_DENIED_FAIL\n");
        return ok ? 0 : 1;
    }

    ok = syscall_hardware(HACKOS_HW_COUNT, 0, &info) == HACKOS_ERROR && ok;
    for (uint64_t section = 0; section < HACKOS_HW_COUNT; ++section)
    {
        ok = syscall_hardware(section, ~0ULL, &info) == 0 && ok;
        ok = syscall_hardware(section, 0, (HackosHardwareInfo*)0x100000) == HACKOS_ERROR && ok;
        ok = syscall_hardware(section, 0, (HackosHardwareInfo*)0x400000000000) == HACKOS_ERROR && ok;
        ok = syscall_hardware(section, 0, (HackosHardwareInfo*)0x4000001fffff) == HACKOS_ERROR && ok;
        ok = syscall_hardware(section, 0, &info) == 1 && ok;
    }

    ok = syscall_hardware(HACKOS_HW_SYSTEM, 0, &info) == 1 && info.system.pciAvailable && !info.system.pciDropped && ok;
    uint32_t count = info.system.pciStored;
    unsigned usb = 0;
    for (uint64_t i = 0; i < count; ++i)
    {
        ok = syscall_hardware(HACKOS_HW_PCI, i, &info) == 1 && info.pci.vendor != 0xffff && ok;
        if (info.pci.classCode == 12 && info.pci.subclass == 3)
        {
            ++usb;
        }
    }

    ok = usb >= 3 && syscall_hardware(HACKOS_HW_PCI, count, &info) == 0 && ok;
    ok = syscall_hardware(HACKOS_HW_MEMORY, 0, &info) == 1 && info.memory.freeBytes > 0 && info.memory.usedBytes > 0 &&
         info.memory.firmwareUsableBytes < 2ULL * 1024 * 1024 * 1024 && info.memory.pageSize == 4096 && ok;
    ok = syscall_hardware(HACKOS_HW_CPU, 0, &info) == 1 && info.cpu.discoveredCpus == 4 && info.cpu.activeCpus == 1 &&
         ok;
    ok = syscall_hardware(HACKOS_HW_NETWORK, 0, &info) == 1 && info.network.available <= 1 &&
         info.network.linkUp <= 1 && info.network.arpState <= HACKOS_ARP_TIMEOUT &&
         info.network.attempts <= 5 && info.network.reserved == 0 && ok;
    for (unsigned i = sizeof(info.network); i < sizeof(info); ++i)
    {
        ok = info.bytes[i] == 0 && ok;
    }
    HackosClock before = {}, after = {};
    ok = syscall_clock(&before) == 0 && before.year >= 2026 && before.month >= 1 && before.month <= 12 &&
         before.day >= 1 && before.day <= 31 && before.hour <= 23 && before.minute <= 59 && before.second <= 59 && ok;
    syscall_sleep(20);
    ok = syscall_clock(&after) == 0 && after.ticks > before.ticks && ok;
    User::Print(ok ? "UTILITIES_ABI_OK\n" : "UTILITIES_ABI_FAIL\n");
    return ok ? 0 : 1;
}
