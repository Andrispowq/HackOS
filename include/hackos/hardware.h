#pragma once
#include "hackos/types.h"

enum HackosHardwareSection : uint64_t
{
    HACKOS_HW_SYSTEM,
    HACKOS_HW_CPU,
    HACKOS_HW_MEMORY,
    HACKOS_HW_DISK,
    HACKOS_HW_PCI,
    HACKOS_HW_PS2,
    HACKOS_HW_DISPLAY,
    HACKOS_HW_ACPI,
    HACKOS_HW_MEMORY_MAP,
    HACKOS_HW_COUNT
};

// Copied, pointer-free records. hardware(section, index, out) returns 1 for
// a record, 0 at the end, or an error. Singleton sections accept index 0.
union HackosHardwareInfo
{
    struct
    {
        uint32_t uefi, pciAvailable, pciStored, pciDropped;
        uint32_t acpiRevision, timerHz;
        char oem[8];
    } system;

    struct
    {
        char vendor[16], brand[64];
        uint32_t family, model, stepping, discoveredCpus, activeCpus;
        uint32_t featuresEdx, featuresEcx, extendedEdx;
    } cpu;

    struct
    {
        uint64_t freeBytes, usedBytes, reservedBytes, firmwareUsableBytes;
        uint32_t entries, pageSize;
    } memory;

    struct
    {
        uint64_t sectors;
        uint32_t slot, type, sectorSize, bootDevice;
    } disk;

    struct
    {
        uint16_t segment, vendor, device;
        uint8_t bus, slot, function, classCode, subclass, interface, revision, headerType;
    } pci;

    struct
    {
        uint32_t keyboardRegistered, mouseEnabled, mouseId;
    } ps2;

    struct
    {
        uint32_t width, height, bitsPerPixel, pitch;
    } display;

    struct
    {
        char signature[8], oem[8], tableId[12];
        uint32_t length, revision, oemRevision;
    } acpi;

    struct
    {
        uint64_t address, size;
        uint32_t type, usable;
    } region;

    uint8_t bytes[128];
};

static_assert(sizeof(HackosHardwareInfo) == 128, "hardware ABI");
