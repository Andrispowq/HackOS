# HackOS contributor guide

## Project and current state

HackOS is a hobby x86-64 operating system with two boot paths: a custom BIOS
MBR/stage-1/stage-2 chain and a UEFI application built against vendored GNU-EFI
3.0.12. The kernel and userspace library are freestanding C++; BIOS stage 2 and
the UEFI loader are C; assembly uses NASM Intel syntax, except GNU-EFI's own
GNU assembler sources. There is no hosted C/C++ standard library in the kernel.

Paging, allocation, interrupts, PS/2 input, framebuffer graphics, ACPI/PCI
discovery, ATA/AHCI storage, FAT32, and a small VFS have implementations. This is
experimental code, not a claim that every device or boot path currently works.
The [documentation index](docs/index.md) links to the system and development
guides. Keep those pages consistent with changes to component contracts.

Userspace is unfinished: `InitialiseTasking()` is commented out in
`kernel/src/init/kernelInit.cpp`, `Process` hard-codes `isUser = false`, and the
ELF loader maps segments through `KernelDirectory` and calls their entry points
directly. Do not describe this as working isolated ring-3 process execution.

## Where to look

| Area | Starting points |
| --- | --- |
| Image creation and QEMU | Root `Makefile`; `setup` and `setup.sh` are duplicate legacy Debian installers |
| BIOS MBR and first stage | `boot/BIOS/mbr/boot_loader.asm`, `boot/BIOS/stage_1/boot_loader.asm` |
| BIOS real-to-long-mode transition | `boot/BIOS/stage_2/src/long_mode/second_stage_entry.asm` and its included assembly |
| BIOS C loader and ELF loading | `boot/BIOS/stage_2/src/second_stage.c`, `src/elf_loader/` |
| UEFI loader | `boot/UEFI/gnu-efi/bootloader/main.c`; keep its custom code distinct from the surrounding vendored library |
| Kernel entry and initialization | `kernel/src/kernel.cpp`, `kernel/src/init/kernelInit.{h,cpp}` |
| CPU structures, exceptions, IRQs | `kernel/src/arch/x86_64/{gdt,interrupts,timer,syscall}/` |
| Paging and physical allocation | `kernel/src/arch/x86_64/paging/` |
| Heap and boot memory maps | `kernel/src/memory/` |
| ACPI and PCI enumeration | `kernel/src/acpi/`, especially `pci/pci.cpp` |
| Block devices | `kernel/src/drivers/{device,ata,ahci}/` |
| PS/2 and experimental USB | `kernel/src/drivers/{keyboard,mouse,usb}/`; PCI OHCI/EHCI/XHCI branches are placeholders |
| Graphics, console, windows | `kernel/src/drivers/screen/`; packed RGB colour macro is `COLOUR` in `screen.h`, PSF1 font rendering is in `screen.cpp` |
| Filesystems | `kernel/src/fs/filesystem.*`, `vfs.*`, `fat32/`; low-level FAT operations are in `fat32driver.*` |
| Program loading and scheduling | `kernel/src/proc/{elf,tasking}/` |
| Kernel shell | `kernel/src/shell/shell.cpp` |
| Userspace runtime and programs | `libs/libc/`, `libs/shell/`, `libs/test/`; `util/libc/Makefile` builds and copies programs into image inputs |
| Minimal support routines | `kernel/src/lib/`; similar code also exists in BIOS stage 2 and `libs/libc/`, with independent build rules |
| Assets and old experiments | `util/fonts/zap-light16.psf`, `util/usertest/usertest.elf`, `util/tools/`; the retained ext2 image and generated initrd images are not used by the current image recipes |

Read [system documentation](docs/system/index.md) for component behavior,
[build instructions](docs/development/building.md) for toolchain constraints,
and [TODO](docs/TODO.md) for priorities. The [testing guide](docs/development/testing.md)
describes `tests/`, test containers, and the GitHub Actions workflow.

## Boot flow and contracts

BIOS: MBR relocates from `0x7c00` to `0x0600`, finds the active partition, and
loads stage 1 at `0x7c00`. Stage 1 loads stage 2 at `0x8000`. Stage 2 reads the
font to `0x6000` and kernel ELF to `0x20000`, requests 1920x1080x32 VBE graphics,
collects the memory map, enables long mode/SSE, and calls `loader_main`.
The ELF loader places the kernel at its linked addresses and calls `kernel_main`.

The BIOS image uses 512-byte sectors and fixed offsets shared by the root
Makefile, MBR partition table, and stage-1/stage-2 assembly:

| Payload | Start LBA | Reserved sectors | Maximum bytes |
| --- | ---: | ---: | ---: |
| MBR | 0 | 1 | 512 |
| Stage 1 | 1 | 1 | 512 |
| Stage 2 | 2 | 64 | 32,768 |
| PSF font | 66 | 12 | 6,144 |
| Stripped kernel ELF | 78 | 256 | 131,072 |
| FAT32 data partition | 334 | 93,750 | 48,000,000 |

The current image recipe silently truncates oversized payloads through `dd
count=...`. Any change to these limits must update all readers and writers.
FAT32 discovery on BIOS reads the relocated MBR at `0x0600`; on UEFI it assumes
a filesystem beginning at LBA 0.

UEFI: `efi_main` opens `kernel.elf`, loads ELF segments, obtains GOP graphics,
a PSF1 font, ACPI and memory-map data, calls `ExitBootServices`, then calls the
ELF entry using the SysV ABI. `kernel/startup.nsh` launches `EFI/BOOT/main.efi`.
See [boot documentation](docs/system/boot.md) for current font-path,
firmware-entry, and memory-map problems.

The boot handoff structure is separately declared in BIOS `second_stage.c`,
UEFI `bootloader/main.c` (`BOOTINFO`), and kernel `init/kernelInit.h`
(`KernelInfo`). Preserve field order, widths, padding, and the nested
framebuffer/font/memory-map layouts across all three. The BIOS/EFI memory type
numbers differ; the kernel uses `booted_from_BIOS` to select interpretation.

`kernel_main` initializes display/GDT/IDT/paging, then heap, devices/filesystems,
ACPI/PCI, filesystem/program smoke code, shell, windows, syscalls and IRQs.
It draws the backbuffer and halts in a loop. The separate `kernel_task` contains
the window-update loop and shell-program launch, but tasking is currently disabled.

## Build and validation

- Read the [build guide](docs/development/building.md) before trying to build.
  `make -C tests test` runs host checks, boot-asset checks, and a BIOS MBR/stage-1
  probe. Full BIOS/UEFI builds and GDB boot milestones are separate targets; see
  `tests/Makefile` and `.github/workflows/tests.yml`. Existing defects are reported
  as failures. A successful compiler invocation is not a successful boot.
- Host tests enforce standard C memory/string semantics and compile real sources
  with explicit hardware/allocator adapters. Do not weaken assertions to preserve
  bugs. LLVM coverage covers only the linked C/C++ subset. ASan needs a compatible
  x86-64 runtime; `--sanitizers undefined` is an explicit local fallback, not CI.
- BIOS is selected by default: `make build_bios`. UEFI is selected with
  `make TARGET_BIOS=false build_efi`. `make all` builds and launches QEMU.
  Bare `make` selects `init`, which cleans and also builds GNU-EFI even for BIOS.
- The current Makefiles invoke host `gcc`, `ld`, `objcopy`, and `ar`.
  An ELF target toolchain is required; macOS host tools are not substitutes.
  `CC=x86_64-elf-gcc LD=x86_64-elf-ld` overrides only part of the toolchain;
  literal `objcopy`/`ar` recipes still need fixing. `CROSS_COMPILE` currently
  works in GNU-EFI, not in the project-owned Makefiles.
- Build directories are incompletely created outside `clean` targets. Do not
  run `make -j` until link/object dependencies are fixed: `link` is currently a
  sibling prerequisite of the objects. Headers also lack generated dependencies.
- Switching compilers requires removing stale object files. For exploratory
  builds, use a disposable copy excluding `.git`, `bin` and `bin-int` directories
  so retained binary inputs and firmware variables stay untouched.
- Kernel compilation fails because
  `AHCIDevice::GetType()` lacks the `const` required by `Device::GetType() const`;
  BIOS stage 2 fails in `cpu/rsdp.c` on undeclared `strncmp` and invalid implicit
  pointer/integer conversions. The UEFI loader's `<elf.h>` is unavailable in the
  bare toolchain. `libs/test` also includes a missing `framebuffer/framebuffer.h`.
  MBR and stage 1 must each fit in 512 bytes.
  Update this guidance when the build blockers are fixed.
- For boot changes, test BIOS and UEFI separately in QEMU using the existing
  `q35` device topology. Check ELF class, machine, entry point, and load segments
  with target `readelf`, and compare BIOS payload sizes to the table above.
  Use a disposable image for FAT32 write tests and a copied OVMF VARS file.
- Screen output is graphical; `-nographic` alone does not expose the framebuffer
  as a text console. Use a graphical QEMU window or VNC for visual smoke checks.
- The legacy `make install` writes directly to `/dev/sdb`; it is not a build or
  emulator test target. Do not use it for routine validation.

## Editing guidance

Keep changes local to the subsystem being worked on and follow its existing
formatting. Preserve freestanding assumptions, x86-64 calling conventions,
interrupt-frame layouts, linker addresses and NASM/C symbol names. Scheduler
assembly walks frame pointers, so changing optimization/frame-pointer flags
requires reviewing that code. Syscall numbers and argument conventions must
stay synchronized between `kernel/src/arch/x86_64/syscall/` and
`libs/libc/src/kernel_interface/`.

Do not sweep-edit the vendored GNU-EFI tree. Keep firmware and the PSF font as
inputs. `util/usertest/usertest.elf` has no source/build rule in this checkout and
must remain available until replaced reproducibly. Generated outputs are ignored
and untracked. `ext2.img` remains tracked because its contents have no regeneration
recipe. See [artifacts and fixtures](docs/development/artifacts.md) before changing
binary inputs, and do not restore generated binaries to hide missing dependencies.
