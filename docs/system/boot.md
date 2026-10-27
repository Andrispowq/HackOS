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
   reads the kernel ELF to `0x20000` and the PSF font to `0x6000`. It requests
   1920x1080, 32-bit VBE graphics, gathers a memory map, switches through
   protected mode into long mode, enables SSE, and calls `loader_main`.
4. [loader_main](../../boot/BIOS/stage_2/src/second_stage.c) prepares paging,
   finds the ACPI root, initializes the font/display, and uses the
   [BIOS ELF loader](../../boot/BIOS/stage_2/src/elf_loader/elf.c) to load the
   kernel's segments and call its entry point.

The loader is tied to the generated disk layout. It does not find the kernel
by walking FAT32 directories.

## BIOS image layout

All sectors are 512 bytes. The first MBR partition contains the boot payloads;
the second contains the FAT32 filesystem.

| Payload | Start LBA | Sector allocation | Byte limit |
| --- | ---: | ---: | ---: |
| MBR | 0 | 1 | 512 |
| Stage 1 | 1 | 1 | 512 |
| Stage 2 | 2 | 64 | 32,768 |
| PSF font | 66 | 12 | 6,144 |
| Stripped kernel ELF | 78 | 768 | 393,216 |
| FAT32 data | 846 | 93,750 | 48,000,000 |

These numbers appear in [make_image.py](../../tools/make_image.py), the MBR
partition table, and the stage-1/stage-2 assembly. Changing only one breaks the
image contract. Packaging rejects oversized payloads and writes complete files.
The boot kernel has all symbols stripped; a separate ELF retains debug symbols.
Stage 2 reads twelve 64-sector chunks into `0x20000..0x7ffff`, below the
VGA/firmware region. Step 14 expanded this reservation from 128 KiB to 384 KiB.
A bounded FAT32 kernel-file loader remains a follow-up in TODO.

The relocated MBR at `0x0600` is also used later by the kernel's FAT32 discovery.
That memory must stay intact while the filesystem is initialized.

## UEFI path

The [UEFI application](../../boot/UEFI/gnu-efi/bootloader/main.c) uses GNU-EFI
to open `kernel.elf` on its volume, validate the basic ELF identity, allocate
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

## Shared boot information

The structure is declared three times: `KernelInfo` in
[BIOS stage 2](../../boot/BIOS/stage_2/src/second_stage.c), `BOOTINFO` in the
[UEFI application](../../boot/UEFI/gnu-efi/bootloader/main.c), and `KernelInfo` in
[the kernel](../../kernel/src/init/kernelInit.h).

| Field | Meaning |
| --- | --- |
| `framebuffer` | Pixel-buffer address, dimensions, bits per pixel, and pitch |
| `font` | Pointer to PSF1 metadata and glyph storage |
| `memMap` | Pointer to converted memory-map entries |
| `memMapEntrySize` | Entry size supplied by the loader |
| `rsdp` | ACPI root pointer |
| `memMapEntries` | Entry count, currently only eight bits |
| `rsdp_version` / `rsdp_revision` | Loader-specific ACPI version field |
| `booted_from_BIOS` | Selects BIOS versus EFI interpretation in the kernel |

Field order, integer widths, pointer widths, alignment, and the layouts of
nested structures must agree. BIOS and EFI memory types use different numeric
values; converting the entry shape does not normalize those values. The kernel
currently uses the boot-mode flag to select usable memory types.

The loaders' referenced data must remain accessible after the kernel replaces
page tables. A shared ABI header and layout assertions would make this contract
less fragile, including a wider memory-map entry count.

## Linker contracts

[Stage 2's linker script](../../boot/BIOS/stage_2/link.ld) places its entry
section at `0x8000`. [The active kernel linker script](../../kernel/link_BIOS.ld)
uses `kernel_main` at a base of `0x00100000` and exports boundaries used by memory
initialization. The kernel Makefile uses this script for both boot paths.

[link_EFI.ld](../../kernel/link_EFI.ld) is currently unused and names `start`
instead. It should not be selected without reconciling that entry symbol.

Related work is listed in [TODO](../TODO.md).

## Loader diagnostics and staging protection

The BIOS stage-2 page allocator reserves the entire ELF staging range
`0x20000..0x80000`, matching all 768 sectors read from LBA 78. An older
`0x50000` cutoff could overwrite staged data when the kernel grew past 192 KiB;
step 18 corrects it without changing the disk layout. Boot regressions compare
both firmware paths' loaded ELF segments, including zeroed BSS, before executing
the kernel entry point.

Both C loaders and kernel entry emit bounded COM1 diagnostics. See
[serial logging](streams.md#serial-diagnostics) for capture and coverage limits.
