# TODO

[Documentation index](index.md) · [Build guide](development/building.md) · [Testing](development/testing.md)

The build and regression tests provide a base for the next changes. Memory
ownership, storage failures and the userspace boundary take priority over adding
more devices.

## Follow-up plan from step 8

Items 1 and 2 are addressed in step 9; built-in parsing in item 3 is addressed
in step 10, and command-program migration in step 11. The credential/permission part of item 4 is addressed in step 13; persistent
accounts, authentication and setup are addressed in step 14.

1. **Slow drawing — addressed in step 9:** replace per-pixel compositor calls
   and byte framebuffer copies with clipped, pitch-aware pixel row copies.
   Paint only border edges instead of filling the entire client underneath each
   composition, and coalesce uploads between paced refreshes. Keep BIOS/UEFI full-screen repaint timing in the window tests.
2. **Black client at larger sizes — addressed in step 9:** the demo waited for
   an empty event queue before repainting a cleared resize buffer. Continuous
   input could postpone drawing indefinitely. Drain bounded event batches and
   repaint even while input continues; retry if geometry changed during upload.
   Verify large normal and maximized client pixels, including under mouse input.
3. **Shell commands — parsing addressed in step 10, programs in step 11:**
   parse command names and arguments once, with quotes/escapes and empty-argument
   preservation. `ps aux` reaches `ps`; built-ins validate arguments and report
   usage. History preserves original text. Commands now run as separate programs
   resolved in `/usr/bin`; `cd` and `history` remain in the shell.
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

## Step 12 — code style cleanup

- [x] Audit project-owned C/C++ for the requested bracing and spacing style: braces
  on loops and else/else-if branches, one execution per line, and blank lines
  between functions and logical blocks. Only a whole single-line if without
  else/else-if may omit braces. Keep this a behavior-preserving cleanup, avoid
  vendored GNU-EFI sources, and rerun host plus BIOS/UEFI regressions.
- [x] Keep users, login and the setup wizard deferred.
- [x] Keep lifecycle diagnostics off the graphical console and add row-based
  `ls`, `-l`, `-a`, combined flags, and explicit FAT metadata.

## Step 13 — users, groups and process permissions

- [x] Add stable boot-session UID/GID allocation, named users/groups, bounded
  supplementary memberships, process credential copies and inheritance.
- [x] Replace boot-shell/executable-path privilege grants with kernel UID checks:
  power and account administration require root; kill requires root or the same
  UID, with idle/boot shell/self still protected.
- [x] Add copied identity/account/group ABI records, process ownership reporting,
  and standalone `id`, `whoami`, `users`, `groups`, `useradd`, `groupadd`, `usermod`
  and root-only `runas` programs. Delegate and restore nested foreground input.
- [x] Leave login/setup for the next requested step. Accounts are in memory for
  this boot only; the initial shell still runs as root. No file permission or
  persistent credential security claim is made.

## Step 14 — persistent accounts, login and setup

- [x] Store versioned account/group/password records in alternating, checksummed
  FAT32 slots with checked writes, flushes and read-back before success.
- [x] Add RDRAND salt generation, PBKDF2-HMAC-SHA256, hidden confirmed password
  entry, root-only `passwd`, first-boot administrator setup and login.
- [x] Start the shell only after authentication and clean up all session
  descendants, windows and file handles on logout.
- [x] Preserve account slots across Docker rebuilds; make normal QEMU runs
  persistent and keep disposable snapshots opt-in.
- [x] Expand the BIOS kernel slot to 384 KiB and move FAT32 to LBA 846.
- [x] Add host crypto/storage fault tests and BIOS/UEFI setup, reboot, password,
  permission, session cleanup and damaged-store regressions.

## Proposed continuation after step 14

Steps 1–14 are complete. The order below is a proposal for future iterations,
not authorization to implement them automatically. A fresh chat should read
`AGENTS.md`, this file, `docs/system/userspace.md`, and the build/testing guides
before starting. Preserve the root `.clang-format` style, leave vendored code
alone, and verify both BIOS and UEFI for each implementation step.

14. **Login and first-boot setup — implemented:** see [accounts and recovery](system/accounts.md)
    for the persistence format, authentication/session contract and limits.

15. **Basic file utilities and a minimal text editor:**
    package these as standalone ring-3 programs in `/usr/bin`, keeping the shell
    lightweight. Do not implement them until this iteration is requested.

    - `xxd <file>`: print file contents in hexadecimal and binary forms. Read the
      whole file into memory with a checked size/allocation limit; no streaming
      or complex buffering is required. Zero-run suppression is optional, not a
      prerequisite. Keep the command's options minimal.
    - `mkdir <path>`: create one directory; recursive parent creation is outside
      this iteration's scope.
    - `rm <file>`: remove one file; directory removal and recursion are outside
      this iteration's scope.
    - `mv <source> <destination>`: rename or move one file. Directory moves and
      cross-filesystem copying are outside this iteration's scope. Define
      destination-conflict behavior explicitly and avoid silent data loss.
    - `edit <file>`: open a very simple text editor with basic text entry and
      editing, Ctrl+S to save, and Ctrl+X to exit. No extra editor features are
      requested. Define a bounded file-size policy and report failed saves.

    Add only the safe, checked userspace filesystem mutation and keyboard/control
    input interfaces needed by these programs. Keep handles process-owned,
    validate user buffers/paths, and report unsupported operations or I/O errors.
    This necessarily brings basic write/create/delete/rename support forward
    from the storage backlog; broader persistence hardening remains there.
    Test the utilities and editor on disposable images under both BIOS and UEFI,
    including saved contents after reboot, invalid paths, destination conflicts,
    and Ctrl+S/Ctrl+X returning cleanly to the shell. Preserve the spacing style
    and keep account/authentication work in its own iteration.

16. **Storage and filesystem permissions:** harden checked write/create APIs,
    process-owned handles and access-mode enforcement. Propagate device, FAT32
    and VFS errors and provide a flush contract. Test full disks, failed writes,
    reboot persistence and interrupted updates on disposable images. Define
    filesystem ownership/permission storage before claiming Unix-style file
    permissions; FAT attributes alone are insufficient.
17. **Userspace usability:** blocking terminal input, job/process-group control,
    a user heap, remaining runtime helpers and shell completion. Split this into
    smaller iterations as needed.

The remaining subsystem lists below are the broader hardening backlog. Step 15
and later work require a new request.
Step 14 host validation passed 163 checks under UBSan, plus boot-asset and
BIOS-chain checks. The local translated ASan runtime fails its empty-program
preflight, so this does not claim ASan coverage. Rerun fresh BIOS/UEFI builds and
boot, account/login, shell/credentials, isolated ring-3 and window suites for
future changes; prior results are not a substitute.

## Boot and memory

- [ ] Share boot-information declarations and add layout assertions across both
  loaders and the kernel. Widen the eight-bit memory-map count.
- [ ] Centralize BIOS slot constants across assembly, image construction and
  tests. Reconcile the unused EFI linker script with the active kernel entry.
- [ ] Remove the fixed BIOS kernel slot as a growth bottleneck. Increasing the
  reservation beyond the current 384 KiB is explicitly acceptable; do not distort otherwise
  sensible kernel changes just to retain that limit. When increasing it, update
  loader read counts/offsets and ELF size bounds, partition extents, image-packaging
  assertions and the documented layout together; check staging-buffer/memory-map limits.
  Prefer investigating `kernel.elf` as a normal FAT32 file if a small, bounded
  loader change can support it. UEFI already loads the kernel as a file; BIOS
  stage 2 currently reads fixed sectors before entering long mode and has no
  filesystem API. Evaluate a minimal read-only FAT32 reader over the existing
  BIOS disk-read path, with file reads before the mode switch, rather than
  importing the kernel's VFS/device stack or requiring a large refactor. Keep
  the ELF-loading/handoff contract, validate file size and cluster chains, and
  handle fragmented, missing, truncated and corrupt kernel files explicitly.
  Test a kernel larger than the old slot, packaging/partition bounds and fresh
  BIOS/UEFI boots. Step 14 enlarged the fixed reservation; the FAT32-file loader remains deferred.
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

- [ ] Add a `Device` capacity query and propagate its checked I/O/flush results
  through all FAT32/VFS operations, including resize/delete. Step 14 checks the
  narrow preallocated account-storage path; general FAT mutation remains incomplete.
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
