# HackOS documentation

HackOS boots an x86-64 kernel through either a custom BIOS loader or a UEFI
application. Most of the kernel runs directly against hardware interfaces, with
small in-tree libraries providing memory, strings, containers, and formatted
output. The system is still under development, especially around process
isolation and userspace.

## System documentation

Start with the [system overview](system/index.md) for the source layout and
initialization order, then follow the component you are working on.

| Component | What it covers |
| --- | --- |
| [Boot and kernel handoff](system/boot.md) | BIOS stages, UEFI loading, disk layout, and the shared boot information |
| [Memory management](system/memory.md) | Firmware memory maps, physical frames, page tables, and the heap |
| [Interrupts and time](system/interrupts.md) | GDT/TSS, exception entry, PIC IRQs, PIT, and RTC |
| [Devices and discovery](system/devices.md) | ACPI, PCI, block devices, PS/2 input, and USB |
| [Filesystems](system/filesystems.md) | Device-to-FAT32 layering, paths, file handles, and persistence |
| [Graphics and input presentation](system/graphics.md) | Framebuffers, colours, fonts, the console, and windows |
| [Programs and userspace](system/userspace.md) | ELF loading, scheduling, syscalls, libc, minimal ring-3 isolation and remaining user APIs |

## Development documentation

The [development index](development/index.md) collects the day-to-day workflow.

| Guide | What it covers |
| --- | --- |
| [Building and running](development/building.md) | Dependencies, cross-compilation, Make targets, QEMU, and test containers |
| [Artifacts and fixtures](development/artifacts.md) | Generated outputs, retained binary inputs, and regeneration paths |
| [Testing](development/testing.md) | Host tests, image checks, QEMU, coverage, and CI |
| [TODO](TODO.md) | Prioritized build, boot, filesystem, memory, and userspace work |

The component pages describe the current implementation and its constraints.
Proposals are identified as future work. When changing a boot contract, public
interface, or build workflow, update the corresponding page alongside the code.

[Project README](../README.md)
