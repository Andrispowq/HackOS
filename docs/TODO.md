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

Steps 1–18 are implemented. The order below is a proposal for future iterations,
not authorization to implement them automatically. A fresh chat should read
`AGENTS.md`, this file, `docs/system/userspace.md`, and the build/testing guides
before starting. Preserve the root `.clang-format` style, leave vendored code
alone, and verify both BIOS and UEFI for each implementation step.

14. **Login and first-boot setup — implemented:** see [accounts and recovery](system/accounts.md)
    for the persistence format, authentication/session contract and limits.

15. **Basic file utilities and a minimal text editor — implemented:**
    Standalone ring-3 programs in `/usr/bin` keep the shell lightweight.

    - `xxd <file>` loads at most 32 KiB into private storage and displays offset,
      hexadecimal, binary and ASCII columns.
    - `mkdir <path>` creates one directory with an existing parent.
    - `rm <file>` removes one regular file, without recursion.
    - `mv <source> <destination>` renames/moves one regular file on the first
      filesystem. The destination must be a new full file path; existing files
      and directories are never overwritten. Directory moves and copying
      between filesystems remain unsupported.
    - `edit <file>` edits at most 32 KiB of ASCII/LF/tab text with arrows,
      Backspace/Delete, Ctrl+S to save and Ctrl+X to exit without saving.
      Failed saves keep the buffer and report an error.

    The bounded mutation ABI validates paths and user buffers, protects account
    storage, rejects open/read-only files, and checks writes/flushes. Read handles
    remain process-owned. BIOS/UEFI disposable-image tests cover invalid paths,
    conflicts, editor input, exact bytes after reboot, disk errors and cleanup.
    See [userspace](system/userspace.md), [filesystems](system/filesystems.md) and
    [testing](development/testing.md). Step 16 adds journaling, interrupted-update recovery, bounded descriptor
    writes and persistent filesystem permissions.

16. **Storage and filesystem permissions — implemented:** checked device capacity,
    VFS mode/token validation, process-owned exclusive writers, bounded writes,
    seek and flush. A versioned reserved-sector permission table stores UID/GID
    and mode separately from FAT attributes. Creation, rename, removal and mode
    changes share the bounded redo journal with FAT/data changes. Kernel checks
    traversal, read/write/execute and parent-directory access; `chmod`, `chown`
    and `ls -l` expose the model. Host and BIOS/UEFI disposable-image tests cover
    failure/full-volume behavior, persistence and interrupted journal recovery.
    See [storage contracts and limits](system/filesystems.md).
17. **Userspace usability — implemented:** blocking decoded terminal input;
    controller-owned process groups, Ctrl-Z/Ctrl-C, `jobs`/`fg`/`bg`/`wait` and
    group termination; a bounded private heap with malloc/calloc/realloc/free,
    C++ allocation, split and userspace formatting; quote-aware Tab completion.
    See [userspace contracts and limits](system/userspace.md). POSIX signals,
    raw scancode events and a general virtual-memory API remain deferred. Pipelines
    follow in step 18.

18. **Streams, pipes, deadlines and logging — implemented:** inherited standard
    streams and local file descriptors, shared file offsets, bounded atomic pipe
    writes, EOF/broken-pipe handling, poll, timed child/job waits and sleep.
    The shell supports four-stage jobs, ordered file/descriptor redirection,
    foreground/background control and exact-byte `cat` streams. COM1 diagnostics
    cover both C loaders, kernel entry, terminal output, lifecycle and faults.
    At step 18 BIOS staging reserved the complete 384 KiB kernel buffer (now
    replaced by FAT32 streaming); boot tests compare
    loaded ELF segments byte for byte. See [streams and waits](system/streams.md).

The remaining subsystem lists below are the broader hardening backlog. Later
work requires a new request.
Step 14 host validation passed 163 checks under UBSan, plus boot-asset and
BIOS-chain checks. The local translated ASan runtime fails its empty-program
preflight, so this does not claim ASan coverage. Rerun fresh BIOS/UEFI builds and
boot, account/login, shell/credentials, isolated ring-3 and window suites for
future changes; prior results are not a substitute.

## Graphical environment

The following milestones turn the existing window demo into a usable desktop.
Milestones 1–4 are implemented; later milestones need a separate request.

1. **Window rendering foundation — implemented:** screen-relative normal sizes
   up to 4096×2160, a shared 64 MiB surface budget, recoverable allocation with
   rollback, generation-checked BEGIN/upload/PRESENT/CANCEL transactions,
   preservation of the last committed image during resize, and blocking timed
   window readiness mixed with streams in `poll`. The demo uses tiled atomic
   frames and sleeps between events. See [the ABI](system/userspace.md#atomic-frame-submission-and-waits).
2. **Input and interaction — implemented:** opt-in physical key-down/up,
   repeat/modifier and US-layout text events; recoverable key/button snapshots;
   Alt-Tab/Shift-Alt-Tab cycling, Alt-Esc terminal focus, Alt-F9/F10 window modes;
   ordered, latched Alt-F4/X close requests with accept/reject; a focused-owner,
   4 KiB session clipboard cleared on logout. GUI Ctrl-C is an application
   shortcut; Ctrl-Alt-Esc explicitly terminates a stuck owner. `window
   --interaction` demonstrates Tab navigation, copy/cut/paste, saving and
   cancellable unsaved-change prompts. See [GUI input](system/userspace.md#gui-input-and-desktop-interaction).
3. **Userspace GUI library — implemented:** `libgui.a` provides clipped drawing
   and PSF text, nested row/column layout, themed labels/buttons/checkboxes,
   selectable text fields, scrolling lists, focus traversal and modal dialogs.
   Its window adapter handles atomic tiled painting, input reset recovery and
   blocking waits. `window --widgets` demonstrates the reusable library.
   See [GUI library](system/gui.md) for API, ownership and bounds.
4. **Desktop and terminal — implemented:** authenticated `DESKTOP` launcher/taskbar,
   session-authorized window controls, eight independent `TERM` endpoints with
   shell job control, per-terminal cleanup, whole-session logout and return-to-login
   recovery. The `console` login option retains a recovery shell.
   See [desktop and terminals](system/desktop.md) for ABI and current limits.
   **Graphical sign-in — implemented:** centered username/masked-password form,
   first-boot confirmation, error dialogs, shutdown/restart and text recovery.
5. **Applications:** file manager complete (bounded read-only previews; file-only
   rename/delete); graphical editor complete (multiline, clipboard, file dialogs,
   unsaved-change confirmation); [settings](system/settings.md) complete for
   hardware pages, account/group editing, password changes and supported recovery-console
   appearance. Session-wide GUI themes and configurable desktop terminals remain follow-ups.
6. **Integrated hardening — in progress:** 6a adds bounded damaged-region
   composition, clipped drawing and debugger-visible render/input latency counters,
   with full-scene equivalence and timing checks on BIOS/UEFI. 6b adds physical
   surface-allocation failure injection and repeated window-limit
   cleanup cycles. Login uses retained partial frames to reduce typing latency;
   the terminal avoids redundant key-transition repaints. 6c adds sustained multi-application desktop cycles with exact resource-baseline
   checks, repeated endpoint saturation/overflow recovery, scalar-oracle clipping
   and padded-presentation coverage. Broader framebuffer-format support and terminal
   polish remain; current graphics coverage is 32-bit BGR.

## Boot and memory

- [x] Share boot-information declarations and layout assertions across both
  loaders and the kernel.
- [ ] Widen the eight-bit memory-map count.
- [ ] Centralize BIOS slot constants across assembly, image construction and
  tests. Reconcile the unused EFI linker script with the active kernel entry.
- [x] Replace the fixed BIOS kernel slot with a minimal read-only FAT32 loader
  for root-directory `KERNEL.ELF`, following the agreed approach below. This
  removes the fixed slot, not all size limits: available RAM, filesystem
  capacity, supported address ranges and FAT32's 4 GiB minus one byte file-size
  limit still apply. Implemented with the shared ELF validator, a real-mode disk
  bridge and low-memory bootstrap tables; see [boot contracts](system/boot.md).
  The agreed plan below records the scope; tables now use six checked pages below
  1 MiB, so accepted kernel destinations cannot overlap loader allocations.

  1. Keep stage 1 loading stage 2 from reserved sectors. Keep the current FAT32
     partition offset (LBA 846) and raw font loading initially; move only
     `kernel.elf` into FAT32 and stop packaging/reading its old raw slot.
     Stage 2 retains its own 32 KiB reservation, which must still be checked.
  2. Add a small read-only FAT32 reader over BIOS disk services, without
     importing the kernel VFS/device stack. Locate the FAT32 partition,
     validate geometry and partition bounds, find the root-directory 8.3 name
     `KERNEL.ELF`, and follow its cluster chain. No long-filename support is
     needed. Support fragmented files and reject missing, truncated, corrupt,
     out-of-range or cyclic chains with bounded traversal and clear failures.
  3. Read and validate the ELF header and program headers first. Check file
     bounds, arithmetic overflow, segment sizes, destination memory and entry
     point before loading. Preserve the existing ELF and `KernelInfo` contracts.
  4. Stream loadable segments into their final memory locations through a small
     reusable low-memory disk buffer, then zero BSS. Remove the fixed
     `0x20000..0x80000` whole-file staging requirement. BIOS reads happen in real
     mode; provide an explicit bridge for copying above 1 MiB, such as returning
     to protected mode for copies between BIOS reads. Adding FAT32 code only to
     the existing 64-bit `loader_main` does not provide BIOS disk access.
  5. Reserve actual kernel segment ranges before allocating page tables or other
     loader memory. Replace the fixed kernel-area reservation below 2 MiB and
     prevent overlap with the loader, stack, bounce buffer, handoff records and
     firmware-reserved memory. Enter long mode and call the validated entry
     point with the existing boot-information ABI.
  6. Align BIOS and UEFI on the same kernel filename, ELF validation/loading
     rules and handoff, sharing logic where practical. Keep BIOS FAT32/disk
     access and VBE/E820 separate from UEFI firmware file access and GOP/memory
     services. A single dual-firmware disk image is separate work.
  7. Update image packaging, loader constants, ELF bounds, boot-asset assertions
     and boot documentation together. Verify a kernel larger than 384 KiB,
     fragmented files, malformed-file rejection and partition bounds. Run fresh
     BIOS/UEFI boots and compare loaded segments and zeroed BSS before entry;
     run the interactive and lifecycle regression suites required by AGENTS.md.

- [x] Share BIOS/UEFI ELF validation and reject malformed kernel files; free partial
  UEFI kernel allocations on load failure.
- [ ] Extend firmware tests to unsupported GOP formats and map overflow.
- [x] Add bounded COM1 diagnostics before kernel graphics and in both C loaders (step 18).
- [ ] Extend serial diagnostics to BIOS real-mode MBR/stage-1/video failures.
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

- [x] Add device capacity and checked I/O/flush propagation, including resize/delete,
  bounded journal recovery and VFS mode/token checks (step 16).
- [ ] Discover partitions from each selected device rather than relying on the
  boot MBR, a fixed device index or an unpartitioned UEFI volume.
- [x] Test media faults, full volumes, remounts and interrupted journal updates
  (step 16).
- [ ] Expand fragmented/corrupt-media coverage and offline repair tooling beyond
  the bounded journal and checked chain traversal.
- [x] Define shared readers/exclusive writers and reject path/permission mutations
  while process-owned handles are live. Trusted raw kernel objects still require
  explicit lifetime management.
- [ ] Add Unicode/longer filename support and more malformed LFN fixtures.
- [x] Bound AHCI DMA chunks, split ATA transfers and reject capacity/count overflow.
- [ ] Expand real-hardware controller recovery and DMA setup audits.
- [x] Enforce VFS modes with opaque tokens and extend process-owned handles to writes.

## Userspace and scheduling

- [x] Optimize login password hashing/verification: reuse fixed padded digest
  blocks and word-form intermediates in PBKDF2-HMAC-SHA256 while retaining all
  600,000 iterations and the existing account format. The guest timing harness
  separates hashing from Enter-to-shell latency; see the testing guide.

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
- [x] Add bounded whole-file saves, file/directory creation, file deletion/moves,
  and editor controls to the safe ABI (step 15).
- [x] Add blocking input, process-group/job control, a user heap and allocation/formatting
  runtime helpers (step 17).
- [x] Add opt-in GUI key-up/modifier/text events and state recovery; decoded keys remain the terminal contract.
- [x] Add minimize/maximize/restore, a restore bar, and signed mouse-wheel
  events to the owned window API and standalone demo.
- [x] Add bounded window dragging/resizing, focus/z-order, per-window input
  events and overflow recovery.
- [x] Port the old window test to `USR/BIN/WINDOW` using process-owned handles,
  bounded pixel copies, close polling, and automatic lifecycle cleanup.
- [x] Reclaim private heap pages on every process exit path (step 17).
- [ ] Add priorities and broader scheduler stress tests.
- [ ] Replace binary-only `usertest.elf` with a source-built fixture.
- [x] Add shell completion and a blocking terminal input API for programs (step 17).
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
