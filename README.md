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

An automatically started standalone ring-3 PS/2 shell runs on both boot paths.
Try `help`, `ls`, `ps` or `print tick`. Run `user USR/BIN/RING3` for the minimal
isolated ring-3 print/preemption/exit demo. Both `./` and `user` accept arguments and wait for isolated programs to exit
(append `&` for background execution); `shutdown` requests kernel ACPI
power-off. `./USR/BIN/INPUT` demonstrates interactive input; Ctrl-C stops the
foreground program, and Up/Down recalls the latest 256 shell commands. USB support is experimental,
and there is no networking stack.

## Getting started

With Docker and Compose installed, run:

```sh
docker compose up --build bios
```

Open [the browser console](http://localhost:6080/vnc.html?autoconnect=true&resize=scale).
For UEFI, stop BIOS and run `docker compose --profile uefi up --build uefi`.
The [build guide](docs/development/building.md) covers native builds, output
files, debugging, and the automatically built cross-compiler.

Generated binaries and copied programs are ignored and should be rebuilt from
source. The [artifact guide](docs/development/artifacts.md) explains which binary
inputs remain in the repository and how the generated files fit into the build.

Run `make -C tests test` for host checks, boot assets, and the BIOS loader-chain
probe. The [testing guide](docs/development/testing.md) covers dependencies,
Docker, coverage reports, full BIOS/UEFI boot checks, and GitHub Actions. Host tests enforce standard C semantics; BIOS and UEFI boot checks require
kernel entry and repeated timer interrupts. `make -C tests test-shell FIRMWARE=bios`
(or `uefi`) exercises keyboard input, the file ABI, context switching and task exit
after a fresh image build.

## License

HackOS is distributed under the [Apache License 2.0](LICENSE). Vendored GNU-EFI
sources carry their own license notices; retain those notices when changing or
redistributing that code.

Launch `./USR/BIN/WINDOW` for the standalone ring-3 window demo. It uses owned
handles and checked pixel uploads; close X, press Q, or use Ctrl-C to return.
