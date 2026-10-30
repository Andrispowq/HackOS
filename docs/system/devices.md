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
enter the experimental USB path. Intel 82540EM devices enter the experimental
[E1000 Ethernet/ARP path](networking.md). Device-name tables provide human-readable
descriptions.

[MADT handling](../../kernel/src/acpi/apic/madt.cpp) records enabled local APIC
IDs and local/I/O APIC addresses. It discovers processors but does not implement
secondary-processor startup or a multiprocessor scheduler. Printing an HPET
table address also does not mean an HPET timer driver is active.

## Block devices

[Device](../../kernel/src/drivers/device/device.h) defines checked `Read`, `Write`, `Flush` and
`GetType`, plus `SectorCount` in 512-byte sectors. Instances are stored in a fixed registry with room for 16 devices.
The current implementations interpret the I/O `size` argument as a **sector
count**, while filesystem-level sizes are byte counts.

| Driver | Implementation |
| --- | --- |
| [ATA PIO](../../kernel/src/drivers/ata/) | Performs port-based sector I/O; rejects out-of-range LBA/count values; writes use 16-bit words and explicit cache flush |
| [AHCI](../../kernel/src/drivers/ahci/) | Maps controller registers, discovers ports, prepares command/FIS buffers, and submits DMA requests |

Kernel startup registers PCI-discovered devices, adds an ATA device, and mounts
FAT32 using `devices[0]`. This makes enumeration order part of boot behavior;
there is no general boot-volume selection mechanism yet.

Transfers return `bool`, with false meaning incomplete I/O; flush support defaults
to failure. The account store and journaled FAT mutation paths propagate these results.
ATA IDENTIFY supplies the usable LBA28 capacity; PIO transfers split into at
most 255 sectors. AHCI IDENTIFY requires LBA48 and supplies its capacity; both
implementations reject overflow and out-of-capacity requests before transfer. AHCI splits transfers into at most eight sectors
using one persistent physical bounce page per port, including the upper address
bits in DMA descriptors. Polling is bounded; a timed-out port stops reusing its
DMA memory. Broader hardware recovery and discovery remain TODO.

## PS/2 input

The [keyboard driver](../../kernel/src/drivers/keyboard/keyboard.cpp) reads
translated set-1 scancodes from port `0x60`. Its allocation-free decoder tracks
both Shift keys and Caps Lock, bounds-checks make/break codes, and handles or
ignores extended sequences. Decoded keys join the ordered input ingress queue.
Idle routes them to the focused window, or a 255-character terminal FIFO. The
terminal owner (login, authenticated shell or foreground program) drains it through the input
syscall and edits a bounded line in ring 3,
and dispatches commands on Enter. The kernel reads the queue with interrupts
briefly disabled; background processes cannot consume terminal input. Extended Up/Down keys become
events, and either Ctrl key plus C cancels shell input or requests termination of
the focused window owner or terminal foreground process at a safe scheduling point. Overflow discards input instead of executing
a truncated command. The keyboard layout is US ASCII.

The [mouse driver](../../kernel/src/drivers/mouse/mouse.cpp) configures the PS/2
controller, negotiates IntelliMouse ID 3 with sample rates 200/100/80, and
queues complete position/button/wheel packets. Commands use bounded ACK/resend
handling. A failed wheel probe resets the mouse to the basic three-byte mode;
ID 3 uses a signed fourth byte for wheel steps. The packet decoder has host
coverage for both protocols, synchronization and signed deltas.
The compositor draws the cursor after windows.
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

Window interaction uses an ordered fixed input queue shared by PS/2 mouse and
translated key input. IRQ handlers only enqueue; idle routes events to windows
or the terminal and updates focus/capture. Mouse packets are processed when
their final byte arrives, and the cursor is drawn by the compositor rather than
the IRQ. See [window events](userspace.md#window-interaction-and-events).

## Console rendering

The console scales the bundled PSF1 bitmap to even pixel heights from 8 to 64
(default 30), with a half-height cell width. Glyph drawing clips to framebuffer
bounds and respects scanline pitch; scrolling moves complete text rows and clears
the remaining bottom strip. Window titles and controls retain their native font.
The `terminal` utility controls font height and RGBA colours; see
[terminal appearance](utilities.md#terminal-appearance) for alpha, ownership and
lifetime. A successful setting clears the visible console without deleting shell
history. Queries report the resulting complete-cell rows and columns.

## Userspace inventory

Root can run `sysinfo` for a full report, or select `cpu`, `memory`, `memory-map`,
`disk`, `pci`, `usb`, `ps2`, `display`, `acpi` or `system`. `ps/2` and `pc/2` alias
`ps2`. Reports use ordinary stdout, including pipes and file redirection.
See [utility commands and ABI](utilities.md).

PCI discovery retains up to 512 pointer-free function records with segment,
bus/device/function, vendor/device IDs, class/subclass/interface, revision and
header type. It checks the vendor absence sentinel, includes the MCFG end bus,
and scans all device slots even if slot zero is absent. Excess records are counted
and reported as omitted; hardware reports do not rescan or write PCI registers.
No MCFG means the PCI/USB inventory is reported unavailable. USB reports describe
controllers and current driver limitations; they do not claim attached-device
support. PS/2 reports distinguish keyboard IRQ registration from mouse enable ACK.
