# HackOS

HackOS is a hobby x86-64 operating system with custom BIOS and UEFI bootloaders
and a freestanding C++ kernel. The long-term idea is to build a small hacking
toolset, starting with the operating system underneath it.

## Documentation

Start with the **[documentation index](docs/index.md)**.

- [System architecture](docs/system/index.md): bootloaders, memory, interrupts,
  devices, filesystems, graphics, and userspace.
- [Building and running](docs/development/building.md): toolchain requirements,
  Make targets, disk images, QEMU, and the test containers.
- [Development guide](docs/development/index.md): source conventions, generated
  artifacts, tests, and CI.
- [TODO](docs/TODO.md): the most important issues and the order to tackle them.

[AGENTS.md](AGENTS.md) is a compact source map and contributor guide for working
in the repository.

## Current state

The kernel includes paging and allocation, GDT/IDT and IRQ setup, PS/2 keyboard
and mouse drivers, framebuffer graphics and a console, ACPI/PCI discovery,
ATA/AHCI storage, FAT32, and a small VFS. There are also window primitives,
an ELF loader, a scheduler, and an experimental userspace library.

Userspace is unfinished. Tasking is disabled in the normal startup path, and
program loading does not yet provide isolated ring-3 execution. USB support is
experimental, and there is no networking stack.

## Getting started

Read the [build guide](docs/development/building.md) before running Make. The
build currently needs toolchain and source fixes; having a compiler installed
is not enough for a clean build. BIOS and UEFI have separate build targets and
must be checked independently.

Generated binaries and copied programs are ignored and should be rebuilt from
source. The [artifact guide](docs/development/artifacts.md) explains which binary
inputs remain in the repository and how the generated files fit into the build.

Run `make -C tests test` for host checks, boot assets, and the BIOS loader-chain
probe. The [testing guide](docs/development/testing.md) covers dependencies,
Docker, coverage reports, full BIOS/UEFI boot checks, and GitHub Actions. Tests
currently expose existing defects; production source fixes are still needed.

## License

HackOS is distributed under the [Apache License 2.0](LICENSE). Vendored GNU-EFI
sources carry their own license notices; retain those notices when changing or
redistributing that code.
