#pragma once

#include "acpi/pci/pci.h"

namespace Net
{
// Called once per matching PCI function, before user page tables are created.
void InitialiseE1000(PCI::PCIDeviceHeader* device);
// Idle-only bounded work; no allocation, sleep, or interrupt-context processing.
void Poll();
// Copied diagnostics through the existing root-only hardware query.
// Internal transmit path; caller holds a preemption guard.
bool Transmit(const uint8_t* frame, uint32_t length);
void Snapshot(HackosHardwareInfo& info);
}
