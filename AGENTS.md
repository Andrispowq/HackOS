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

Tasking supports idle plus isolated ring-3 programs. The standalone shell at
`~/USR/BIN/SHELL` starts automatically; idle refreshes the console and reaps
exited tasks. User programs own private frames and page tables, with shared
supervisor-only kernel mappings. Both `./` and `user` launch ring-3 programs with bounded `argc`/`argv` and
foreground waits by default; a separate trailing `&` requests background mode.
Exit results belong to the parent independently of idle resource reclamation.
All active syscalls are user interfaces; kernel code calls internal functions
directly. The boot shell grants input/backspace to its foreground child; only the
shell retains foreground-spawn, kill and power rights. Ctrl-C is deferred from
IRQ context to a safe scheduler point. Shell history is a static 256-command ring.
Userspace windows use process-owned handles and bounded pixel uploads; idle
reclaims them on exit and composites them over the console. `USR/BIN/WINDOW`
is the standalone demo. See `docs/system/userspace.md` for ABI, ownership and lifecycle contracts.

## Where to look

| Area | Starting points |
| --- | --- |
| Image creation and QEMU | Root `Makefile`, `tools/`, `compose.yaml`; `setup.sh` builds the Docker environment |
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
| Standalone shell | `libs/shell/src/main.cpp`; shared line editing in `common/include/hackos/runtime/line_buffer.h` |
| Userspace runtime and programs | `libs/libc/`, `libs/shell/`, `libs/test/`; root Makefile builds them and `tools/make_image.py` packages their outputs |
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

The BIOS image uses 512-byte sectors and fixed offsets shared by
`tools/make_image.py`, the MBR partition table, and stage-1/stage-2 assembly:

| Payload | Start LBA | Reserved sectors | Maximum bytes |
| --- | ---: | ---: | ---: |
| MBR | 0 | 1 | 512 |
| Stage 1 | 1 | 1 | 512 |
| Stage 2 | 2 | 64 | 32,768 |
| PSF font | 66 | 12 | 6,144 |
| Stripped kernel ELF | 78 | 256 | 131,072 |
| FAT32 data partition | 334 | 93,750 | 48,000,000 |

The image packager in `tools/make_image.py` rejects oversized payloads. Any
change to these limits must update all readers, writers and image assertions.
FAT32 discovery on BIOS reads the relocated MBR at `0x0600`; on UEFI it assumes
a filesystem beginning at LBA 0.

UEFI: `efi_main` opens `kernel.elf`, loads ELF segments, obtains GOP graphics,
a PSF1 font, ACPI and memory-map data, calls `ExitBootServices`, then calls the
ELF entry using the SysV ABI. The packaged fallback is `EFI/BOOT/BOOTX64.EFI`;
the font is `UTIL/FONTS/zap-light16.psf`. The loader checks the final memory-map
key, retries a stale key, and rejects more than 255 descriptors.

The boot handoff structure is separately declared in BIOS `second_stage.c`,
UEFI `bootloader/main.c` (`BOOTINFO`), and kernel `init/kernelInit.h`
(`KernelInfo`). Preserve field order, widths, padding, and the nested
framebuffer/font/memory-map layouts across all three. The BIOS/EFI memory type
numbers differ; the kernel uses `booted_from_BIOS` to select interpretation.

`kernel_main` initializes display/GDT/IDT/paging, then heap, devices/filesystems,
ACPI/PCI, windows, syscalls and tasking. It loads the standalone shell before
enabling IRQs. `kernel_main` becomes idle, refreshes the console and reclaims
zombie stacks, private user pages and file handles. Keyboard decoding remains
in the kernel; command parsing and line editing run in the ring-3 shell.

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
- `docker compose up --build bios` builds and runs a browser console on
  `http://localhost:6080/vnc.html`. Stop it before starting the `uefi` service.
  Containers use native amd64/arm64 cross-tools and the same x86-64 TCG guest.
  Source is read-only, build output uses a named volume, and OVMF VARS is copied.
- Bare `make` builds BIOS. `make build_bios` / `make build_efi` build the selected
  loaders, kernel and programs. `make all` also runs QEMU; use `TARGET_BIOS=false`
  for UEFI run/debug. Both direct builds replace `bin/HackOS.img`.
- `CROSS_COMPILE=x86_64-elf-` selects CC/CXX/LD/AR/OBJCOPY consistently. GCC 15.2.0
  and Binutils 2.47 are bootstrapped by `tests/build_toolchain.sh`; PE support is
  required for UEFI. `boot/UEFI/include/elf.h` supplies freestanding ELF types.
- Makefile object/link dependencies and generated C/C++/NASM dependencies support
  parallel compilation. Keep frame pointers, freestanding flags and no-red-zone
  policy. Clean when changing compiler or flags; `tools/build.py` always uses a
  fresh source copy and writes separate `out/bios` / `out/uefi` artifacts.
- The BIOS boot kernel strips symbols to fit its fixed slot; retain the separate
  debug ELF. MBR and stage 1 each must be exactly 512 bytes. Never substitute old
  binaries to hide a build failure.
- For boot changes, test BIOS and UEFI separately in QEMU using the existing
  `q35` device topology. Check ELF class, machine, entry point, and load segments
  with target `readelf`, and compare BIOS payload sizes to the table above.
  Run `tests/qemu_shell.py --firmware bios` and `--firmware uefi` after fresh
  image builds for interactive input, ABI, register-state and task-lifecycle checks.
  Use a disposable image for FAT32 write tests and a copied OVMF VARS file.
- Screen output is graphical; `-nographic` alone does not expose the framebuffer
  as a text console. Use a graphical QEMU window or VNC for visual smoke checks.
- Build and run tools operate only on image files. Do not add physical-disk writes
  to ordinary setup, build or emulator targets.

## Editing guidance

Keep changes local to the subsystem being worked on and follow its existing
formatting. Preserve freestanding assumptions, x86-64 calling conventions,
interrupt-frame layouts, linker addresses and NASM/C symbol names. Interrupt
handlers return a saved `Registers*`; the shared epilogue restores it with
FXRSTOR and IRETQ. Preserve the 704-byte frame and SysV/FXSAVE alignment. Never
yield, sleep or exit inside a `PreemptionGuard`; these single-CPU guards are not
SMP locks. Syscall numbers and argument conventions must
stay synchronized between `kernel/src/arch/x86_64/syscall/` and
`libs/libc/src/kernel_interface/`.

Do not sweep-edit the vendored GNU-EFI tree. Keep firmware and the PSF font as
inputs. `util/usertest/usertest.elf` has no source/build rule in this checkout and
must remain available until replaced reproducibly. Generated outputs are ignored
and untracked. `ext2.img` remains tracked because its contents have no regeneration
recipe. See [artifacts and fixtures](docs/development/artifacts.md) before changing
binary inputs, and do not restore generated binaries to hide missing dependencies.

New code follows the original spacing style: separate functions and logical
blocks with blank lines, one executable statement per line, and braces on loops
and all else/else-if branches. An if without else may omit braces only when its
entire statement fits on one line. Do not bulk-reformat existing code.
