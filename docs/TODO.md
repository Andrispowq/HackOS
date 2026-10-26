# TODO

[Documentation index](index.md) · [Build guide](development/building.md) · [Testing](development/testing.md)

The build and regression tests provide a base for the next changes. Memory
ownership, storage failures and the userspace boundary take priority over adding
more devices.

## Boot and memory

- [ ] Share boot-information declarations and add layout assertions across both
  loaders and the kernel. Widen the eight-bit memory-map count.
- [ ] Centralize BIOS slot constants across assembly, image construction and
  tests. Reconcile the unused EFI linker script with the active kernel entry.
- [ ] Strengthen BIOS ELF validation and both loaders' malformed-input cleanup.
  Extend firmware tests to missing files, unsupported GOP formats and map overflow.
- [ ] Add a serial/debug reporting path for failures before graphics is available.
- [ ] Replace dense physical-page bookkeeping for highly sparse memory maps;
  distinguish reserved/allocated ownership and test high-RAM guests.
- [ ] Define page-table/frame ownership on clone and process exit. Return a
  controlled failure when translating unmapped addresses.
- [ ] Honor heap alignment requests and audit DMA buffers for physical
  contiguity. Test heap splitting, coalescing, exhaustion and allocation failure.
- [ ] Test deliberate exceptions and recovery. Extend context preservation to
  XSAVE/AVX before enabling those CPU features; finish the TSS for ring-3 entry.

## Storage and filesystems

- [ ] Give `Device` a capacity query, explicit I/O result and flush operation;
  propagate failures through FAT32 and VFS, including resize/delete results.
- [ ] Discover partitions from each selected device rather than relying on the
  boot MBR, a fixed device index or an unpartitioned UEFI volume.
- [ ] Test media faults, full volumes, fragmented files, remounts and dirty
  shutdowns. FAT writes are not atomic or journaled.
- [ ] Define metadata lifetime and synchronization for multiple open handles.
- [ ] Add Unicode/longer filename support and more malformed LFN fixtures.
- [ ] Audit AHCI DMA sizes/address translation and ATA sector-count narrowing.
- [ ] Enforce VFS access modes and replace raw object pointers with owned handles.

## Userspace and scheduling

- [ ] Load programs into process-owned memory with appropriate permissions;
  validated ELF ranges alone do not make physical destinations safe.
- [ ] Complete TSS/kernel entry stacks and deliberate ring-3 entry.
- [ ] Choose and finish the syscall mechanism, permit intended user entry and
  establish a stable handle-based ABI instead of exposing kernel objects.
- [ ] Validate/copy user buffers and remove exposed kernel objects.
- [ ] Track program-owned files, allocations and windows for automatic cleanup
  on exit or kill. Add blocking waits, priorities and broader scheduler stress tests.
- [ ] Replace binary-only `usertest.elf` with a source-built fixture.
- [ ] Add shell history/completion and a terminal input API for programs.
  Integrate the window manager without clearing console output.

## Tests and development

- [ ] Extend host tests to the real heap and physical allocator, with failure
  injection and sparse-memory fixtures; keep the hardware adapters explicit.
- [ ] Add guest assertions for exceptions, disk persistence and controlled
  framebuffer scenes. Expand keyboard layouts and overflow stress coverage.
- [ ] Add native amd64/arm64 container build checks and Windows/macOS launch
  checks. Keep full ASan+UBSan on native x86-64 CI.
- [ ] Lock package snapshots and image timestamps if byte-identical build
  artifacts become a requirement; current containers pin compiler sources and
  the base image but still receive distribution package updates.
- [ ] Replace machine-specific editor settings with portable target settings.
- [ ] Inspect/export useful contents of the legacy ext2 image before removing it.
- [ ] Retire the unused initrd experiment or fix its bounds and serialization.

## Later work

- [ ] Support additional framebuffer formats and test window/glyph clipping.
- [ ] Extend USB support beyond the UHCI experiment.
- [ ] Add secondary CPU startup and synchronization before claiming SMP support.
- [ ] Add networking and useful user programs after isolation and storage
  behavior are reliable.
