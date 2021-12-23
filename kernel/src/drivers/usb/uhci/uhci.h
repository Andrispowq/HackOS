#ifndef UHCI_H
#define UHCI_H

#include "lib/stdio.h"
#include "acpi/pci/pci.h"

#include "lib/data_structures/vector.h"
#include "drivers/device/device.h"

void InitialiseUHCI(uint32_t id, PCI::PCIDeviceHeader* header);

#endif