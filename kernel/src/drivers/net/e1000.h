#pragma once

#include "acpi/pci/pci.h"

namespace Net
{
// Called once per matching PCI function, before user page tables are created.
void InitialiseE1000(PCI::PCIDeviceHeader* device);
// Idle-only bounded work; no allocation, sleep, or interrupt-context processing.
void Poll();
// Copied diagnostics through the existing root-only hardware query.
void Snapshot(HackosHardwareInfo& info);
}
