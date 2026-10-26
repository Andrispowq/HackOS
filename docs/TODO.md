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
- [ ] Audit legacy page-table cloning; private user page ownership and exit
  reclamation are implemented. Return a
  controlled failure when translating unmapped addresses.
- [ ] Honor heap alignment requests and audit DMA buffers for physical
  contiguity. Test heap splitting, coalescing, exhaustion and allocation failure.
- [ ] Extend exception coverage beyond the tested user protection faults. Add
  XSAVE/AVX context preservation before enabling those CPU features.

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

- [x] Load minimal static programs into private memory with W/X permissions,
  TSS kernel entry stacks, timer preemption and exit reclamation on BIOS/UEFI.
- [x] Expose a restricted `int 0x80` user ABI for exit and checked literal output;
  reject legacy raw-pointer calls from CPL3.
- [x] Start the standalone ring-3 shell automatically, expose copied terminal/
  process/file data and process-owned read handles, and authorize shell shutdown.
- [x] Add bounded command arguments, parent-owned exit results and blocking
  foreground waits, with explicit background launches.
- [x] Add foreground input ownership, safe Ctrl-C termination, and a fixed
  256-command shell history with Up/Down draft navigation.
- [ ] Add blocking input, process-group/job control, a user heap, file writes and
  owned window handles to the safe ABI. Port remaining legacy runtime helpers.
- [ ] Port the old window test into a separate ring-3 program and package it once
  checked window creation/drawing and ownership exist. The old test (e.g.
  `38dbbf5:libs/test/src/main.cpp`) uses retired calls and kernel buffer pointers.
- [ ] Extend the implemented automatic read-handle cleanup to future allocations
  and windows. Add blocking waits, priorities and broader scheduler stress tests.
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
