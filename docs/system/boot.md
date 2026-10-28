# Boot and kernel handoff

[System index](index.md) · [Build guide](../development/building.md)

Both boot paths load an ELF64 kernel linked at `0x00100000` and transfer control
to its entry point using the x86-64 SysV calling convention. They differ in how
they access disks, select a video mode, and collect firmware information.

## BIOS path

1. The [MBR](../../boot/BIOS/mbr/boot_loader.asm) starts at `0x7c00`, relocates
   itself to `0x0600`, finds an active partition, and loads its first sector.
2. [Stage 1](../../boot/BIOS/stage_1/boot_loader.asm) runs at `0x7c00`, enables
   A20, reads 64 sectors beginning at LBA 2 into `0x8000`, and jumps there.
3. The [stage-2 assembly entry](../../boot/BIOS/stage_2/src/long_mode/second_stage_entry.asm)
   reads the PSF font to `0x6000`. It requests
   1920x1080, 32-bit VBE graphics, gathers a memory map, switches through
   protected mode into long mode, enables SSE, and calls `loader_main`.
4. [loader_main](../../boot/BIOS/stage_2/src/second_stage.c) discovers the unique
   FAT32 MBR partition and opens root-directory `KERNEL.ELF` by its 8.3 name.
   It validates the complete file chain, ELF headers and destination RAM before
   installing its final bootstrap page tables. It streams segment bytes into
   their linked physical addresses, zeroes BSS, and calls the validated entry.

The read-only FAT32 reader checks 512-byte geometry, FAT capacity, partition
bounds, active-FAT selection, root/file chains and the exact file length.
Fragmented files and root directories work; missing, cyclic, truncated, bad or
out-of-range chains fail closed. Long filenames are unnecessary for `KERNEL.ELF`.
The active MBR partition still holds stage 1; FAT32 is a separate partition.

The [disk bridge](../../boot/BIOS/stage_2/src/long_mode/bios_disk.asm) preserves the
boot drive and long-mode calling state, returns through protected mode to real
mode for INT 13h extended reads, and restores long mode before C copies data.
Each read uses a reusable 512-byte bounce buffer at `0x20000`. Firmware PIC
vectors stay intact until all disk reads finish. No whole-kernel staging area
is needed.

The E820 handoff lives at `0x30000` (up to 255 entries; overflow/failure rejects
boot). Six checked RAM pages at `0x40000..0x46000` identity-map the supported
0–4 GiB address space with 2 MiB pages. These tables, the loader/stack, font and
buffers stay below 1 MiB, outside every accepted kernel segment. The kernel
already reserves low memory, preserving the tables until it installs its own.
This avoids allocating bootstrap tables beside the kernel where its early bump
allocator could overwrite them. E820 usable RAM must cover every destination;
overlapping firmware-reserved regions are rejected.

## BIOS image layout

All sectors are 512 bytes. The first MBR partition contains the boot payloads;
the second contains the FAT32 filesystem.

| Payload | Start LBA | Sector allocation | Byte limit |
| --- | ---: | ---: | ---: |
| MBR | 0 | 1 | 512 |
| Stage 1 | 1 | 1 | 512 |
| Stage 2 | 2 | 64 | 32,768 |
| PSF font | 66 | 12 | 6,144 |
| Unused compatibility gap | 78 | 768 | — |
| FAT32 data | 846 | 93,750 | 48,000,000 |

These numbers appear in [make_image.py](../../tools/make_image.py), the MBR
partition table, and the stage-1/stage-2 assembly. Changing only one breaks the
image contract. Packaging rejects oversized payloads and writes complete files.
The boot kernel has all symbols stripped; a separate ELF retains debug symbols.
Both images store the stripped kernel as root-directory `KERNEL.ELF` in FAT32.
The old raw kernel slot is zero-filled and never read. Keeping FAT32 at LBA 846
avoids an unrelated partition-layout migration. Stage 2 retains its 32 KiB
limit, enforced by the linker and packager; kernel size is governed by the
filesystem, available RAM and the ELF address rules below.

The relocated MBR at `0x0600` is also used later by the kernel's FAT32 discovery.
That memory must stay intact while the filesystem is initialized.

## UEFI path

The [UEFI application](../../boot/UEFI/gnu-efi/bootloader/main.c) uses GNU-EFI
to open `KERNEL.ELF` on its volume, apply the shared ELF validation, allocate
memory for loadable segments, and read them into place. It obtains a GOP
framebuffer, loads a PSF1 font, collects the EFI memory map and ACPI root, then
calls `ExitBootServices` before entering the kernel.

The image is a FAT32 volume beginning at LBA 0. The loader is installed as
`EFI/BOOT/BOOTX64.EFI`, the x64 fallback entry. An included `startup.nsh` also
launches that entry when the bundled OVMF starts its shell first. It opens the font at
`UTIL/FONTS/zap-light16.psf` and requires 32-bit BGR GOP output.

File/header/segment reads and allocations are checked before use. The loader
allocates memory-map storage before the final map request, converts that map,
and calls `ExitBootServices` without intervening allocations or logging. It
retries a stale map key once, using the existing buffers. No firmware services
are called after a successful exit. See the
[UEFI boot-services contract](https://uefi.org/specs/UEFI/2.10/07_Services_Boot_Services.html).
The shared eight-bit map count remains a limitation: maps exceeding 255 entries
are rejected rather than silently truncated.

## Shared ELF contract

Both paths use [boot_elf.h](../../include/hackos/boot_elf.h). The kernel must be
little-endian ELF64, x86-64, ET_EXEC, with current version and exact header sizes.
At most 128 program headers are accepted; header tables and file-backed segment
ranges must fit the actual file. PT_INTERP and PT_DYNAMIC are rejected.
Load segments use identical virtual/physical, page-aligned addresses from
1 MiB to below 4 GiB. File size cannot exceed memory size; alignment must be a
power of two with congruent file/address offsets. Rounded destination pages may
not overlap, arithmetic may not overflow, and the entry must lie in file-backed
bytes of an executable segment. BSS is zeroed before entry. BIOS checks E820 RAM;
UEFI reserves each destination with AllocateAddress and frees partial kernel
allocations if loading fails. Both validate all headers before writing segments.

The removed 384 KiB slot is not a claim of unlimited kernels: the packaged
48 MB volume, FAT32's 4 GiB-minus-one-byte file limit, available destination RAM,
128 program headers and the supported physical address range still apply.

## Shared boot information

Both loaders and the kernel include [hackos/boot_info.h](../../include/hackos/boot_info.h).
It defines the 64-byte `KernelInfo`, 24-byte `BootFramebuffer`, 24-byte
`MemoryMapEntry`, and PSF1 header/font records in freestanding C and C++.
Compile-time assertions pin every field offset, record size and alignment,
including eight-byte pointers. The kernel copies the framebuffer data into its
own drawing class; the ABI contains no C++ methods or firmware-specific types.

| Field | Meaning |
| --- | --- |
| `framebuffer` | Pixel-buffer address, dimensions, bits per pixel, and pitch in 32-bit pixels |
| `font` | Pointer to PSF1 metadata and glyph storage |
| `memMap` | Pointer to converted memory-map entries |
| `memMapEntrySize` | Entry size supplied by the loader |
| `rsdp` | ACPI root pointer |
| `memMapEntries` | Entry count, currently only eight bits |
| `rsdp_version` | Actual ACPI RSDP revision byte |
| `booted_from_BIOS` | Selects BIOS versus EFI interpretation in the kernel |

Field order, integer widths, pointer widths, alignment, and the layouts of
nested structures must agree. BIOS and EFI memory types use different numeric
values; converting the entry shape does not normalize those values. The kernel
currently uses the boot-mode flag to select usable memory types.

The loaders' referenced data must remain accessible after the kernel replaces
page tables. The existing eight-bit entry count and natural layout are preserved. Map entries
use byte `size` and 32-bit `attributes`; UEFI retains the low 32 attribute bits.
Widening these fields requires a coordinated ABI change. BIOS passes E820 type
numbers; UEFI passes EFI types. Loader-owned pointers are borrowed by the kernel,
not freed during handoff. Both loaders zero-initialize the handoff record.

The QEMU boot test decodes the raw handoff independently at the exact SysV entry
(`RDI`), checks geometry, font bytes, map stride/count and usable regions, boot
origin and RSDP checksum/revision, then observes IRQ setup and repeated timer
interrupts. It also compares every loaded ELF segment and BSS against the build.

## Linker contracts

[Stage 2's linker script](../../boot/BIOS/stage_2/link.ld) places its entry
section at `0x8000`. [The active kernel linker script](../../kernel/link_BIOS.ld)
uses `kernel_main` at a base of `0x00100000` and exports boundaries used by memory
initialization. The kernel Makefile uses this script for both boot paths.

[link_EFI.ld](../../kernel/link_EFI.ld) is currently unused and names `start`
instead. It should not be selected without reconciling that entry symbol.

Related work is listed in [TODO](../TODO.md).

## Loader diagnostics

Both C loaders and kernel entry emit bounded COM1 diagnostics, including explicit
kernel-file/validation failures. See [serial logging](streams.md#serial-diagnostics)
for capture and coverage limits. Boot regressions compare loaded segment bytes
and BSS before entry on both firmware paths, including a fragmented kernel
larger than the former raw slot.
