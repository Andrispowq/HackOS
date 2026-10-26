# Devices and discovery

[System index](index.md) · [Interrupts](interrupts.md) · [Filesystems](filesystems.md)

Discovery starts from the ACPI root supplied by the bootloader. The kernel
uses ACPI tables to find PCI configuration space and power-management data,
then registers supported storage devices for the filesystem layer.

## ACPI and PCI

[RSDP handling](../../kernel/src/acpi/rsdp.cpp) locates system description tables.
Kernel initialization looks for `MCFG`, `APIC`, `FACP`, and `HPET`.
[FADT/DSDT code](../../kernel/src/acpi/) supports ACPI enablement and shutdown;
this is limited table/AML handling, not a general ACPI interpreter.

[PCI enumeration](../../kernel/src/acpi/pci/pci.cpp) walks MCFG configuration
regions, maps configuration pages, and inspects class, subclass, and programming
interface fields. AHCI controllers create storage devices; UHCI controllers
enter the experimental USB path. Device-name tables provide human-readable
descriptions.

[MADT handling](../../kernel/src/acpi/apic/madt.cpp) records enabled local APIC
IDs and local/I/O APIC addresses. It discovers processors but does not implement
secondary-processor startup or a multiprocessor scheduler. Printing an HPET
table address also does not mean an HPET timer driver is active.

## Block devices

[Device](../../kernel/src/drivers/device/device.h) defines `Read`, `Write`, and
`GetType`. Instances are stored in a fixed registry with room for 16 devices.
The current implementations interpret the I/O `size` argument as a **sector
count**, while filesystem-level sizes are byte counts.

| Driver | Implementation |
| --- | --- |
| [ATA PIO](../../kernel/src/drivers/ata/) | Performs port-based sector I/O; the adapter narrows LBA/count values to its underlying API |
| [AHCI](../../kernel/src/drivers/ahci/) | Maps controller registers, discovers ports, prepares command/FIS buffers, and submits DMA requests |

Kernel startup registers PCI-discovered devices, adds an ATA device, and mounts
FAT32 using `devices[0]`. This makes enumeration order part of boot behavior;
there is no general boot-volume selection mechanism yet.

The interface returns `void`, so storage errors cannot be propagated cleanly
to the filesystem. AHCI also needs careful treatment of physical address
translation, buffer lifetime, and multi-page DMA requests. Virtually adjacent
heap pages are not necessarily physically contiguous.

The `AHCIDevice::GetType` declaration currently lacks the `const` required by
the base interface, which blocks compilation. Driver discovery and correct
transfer behavior are separate milestones.

## PS/2 input

The [keyboard driver](../../kernel/src/drivers/keyboard/keyboard.cpp) reads
scancodes from port `0x60`, tracks modifier state, echoes characters, and
maintains a line buffer. Enter dispatches the line to the kernel's `user_input`
function, which handles power commands or forwards it to the shell.

The [mouse driver](../../kernel/src/drivers/mouse/mouse.cpp) configures the PS/2
controller, assembles packets, updates position/buttons, and draws the cursor.
IRQ1 and IRQ12 connect these drivers to the [interrupt layer](interrupts.md).

These are PS/2 drivers even when QEMU also exposes USB controllers. The kernel
does not have a complete USB keyboard/mouse stack.

## USB

[USB sources](../../kernel/src/drivers/usb/) include descriptor/request types,
controller abstractions, and UHCI initialization. PCI dispatch has placeholders
for OHCI, EHCI, and XHCI. The presence of those controllers in a QEMU command
does not make them supported by HackOS.

When extending discovery, keep table bounds, absent-device handling, BAR
mapping, and ownership of registered devices explicit. Use the existing QEMU
`q35` configuration as a starting point and add hardware variants deliberately.

See [TODO](../TODO.md) for error propagation and boot-device selection work.
