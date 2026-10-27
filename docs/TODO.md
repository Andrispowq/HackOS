# TODO

[Documentation index](index.md) · [Build guide](development/building.md) · [Testing](development/testing.md)

The build and regression tests provide a base for the next changes. Memory
ownership, storage failures and the userspace boundary take priority over adding
more devices.

## Follow-up plan from step 8

Items 1 and 2 are addressed in step 9; built-in parsing in item 3 is addressed
in step 10. Command-program migration and items 4–5 remain deferred; authentication
and setup depend on persistent user storage and credential handling.

1. **Slow drawing — addressed in step 9:** replace per-pixel compositor calls
   and byte framebuffer copies with clipped, pitch-aware pixel row copies.
   Paint only border edges instead of filling the entire client underneath each
   composition, and coalesce uploads between paced refreshes. Keep BIOS/UEFI full-screen repaint timing in the window tests.
2. **Black client at larger sizes — addressed in step 9:** the demo waited for
   an empty event queue before repainting a cleared resize buffer. Continuous
   input could postpone drawing indefinitely. Drain bounded event batches and
   repaint even while input continues; retry if geometry changed during upload.
   Verify large normal and maximized client pixels, including under mouse input.
3. **Shell command/argument separation — built-ins addressed in step 10:**
   parse command names and arguments once, with quotes/escapes and empty-argument
   preservation. `ps aux` reaches `ps`; built-ins validate arguments and report
   usage. History preserves original text. The later migration into programs
   launched from `/usr/bin` remains deferred.
4. **Users, groups, permissions and login:** define stable user/group IDs,
   process credential ownership/inheritance, and kernel-side privilege checks
   (including root-only shutdown/restart). Add persistent account/group storage,
   salted password verification with an appropriate password KDF and entropy,
   and a login/session lifecycle that starts the shell only after authentication.
   Replace boot-shell identity as the privilege model; do not trust shell-only
   checks. Plan logout, failed logins, credential isolation and storage failure
   tests before implementation.
5. **First-boot setup wizard:** after the account/credential foundation exists,
   detect an empty account store and run a CLI wizard to create the initial
   privileged account with a chosen username and confirmed password. Persist it
   safely, reject empty/invalid credentials, and avoid plaintext password storage.
   Subsequent boots must enter login, not repeat setup or start an anonymous
   shell. Define recovery for interrupted setup or corrupt account storage, and
   verify first boot, reboot, failed login and successful login on BIOS/UEFI.

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
  raw-key events to the safe ABI.
  Port remaining legacy runtime helpers.
- [x] Add minimize/maximize/restore, a restore bar, and signed mouse-wheel
  events to the owned window API and standalone demo.
- [x] Add bounded window dragging/resizing, focus/z-order, per-window input
  events and overflow recovery.
- [x] Port the old window test to `USR/BIN/WINDOW` using process-owned handles,
  bounded pixel copies, close polling, and automatic lifecycle cleanup.
- [ ] Extend automatic file/window cleanup to future user allocations. Add
  priorities and broader scheduler stress tests.
- [ ] Replace binary-only `usertest.elf` with a source-built fixture.
- [ ] Add shell completion and a blocking terminal input API for programs.
- [x] Integrate the window manager without overwriting console contents.

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
