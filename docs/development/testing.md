# Testing

[Development index](index.md) · [TODO](../TODO.md)

The suite in `tests/` builds production sources into host executables, checks
boot assets, exercises the BIOS loader chain in QEMU, and attempts clean BIOS
and UEFI builds. Tests report current defects as failures. There are no expected
failure exemptions, and a failed build never falls back to an old binary.

## Running the suite

From the repository root, on an x86-64 machine with Clang/LLVM, Python 3.9+,
NASM, mtools, and QEMU installed:

```sh
make -C tests test
python3 tests/run.py host --case memory_compare
make -C tests build-images FIRMWARE=bios
make -C tests test-qemu FIRMWARE=bios
make -C tests test-shell FIRMWARE=bios
make -C tests test-user FIRMWARE=bios
make -C tests build-images FIRMWARE=uefi
make -C tests test-qemu FIRMWARE=uefi
make -C tests test-shell FIRMWARE=uefi
make -C tests test-user FIRMWARE=uefi
```

`test` runs host tests, asset checks, and the BIOS MBR/stage-1 probe. The full
image build and kernel boot are separate commands and require an ELF cross
compiler and GDB. All output goes under the ignored `tests/out/` directory.
Run without Python `-O` or `PYTHONOPTIMIZE`, since image checks use assertions.

For a consistent Linux environment:

```sh
docker build --platform linux/amd64 --target host \
  -t hackos-tests:host -f tests/Dockerfile tests
mkdir -p tests/out
docker run --rm --platform linux/amd64 --network none \
  --user "$(id -u):$(id -g)" \
  --mount "type=bind,source=$PWD,target=/work,readonly" \
  --mount "type=bind,source=$PWD/tests/out,target=/work/tests/out" \
  hackos-tests:host python3 tests/run.py all
```

The host binary includes real x86-64 NASM routines, so it needs an x86-64
runtime. On Apple Silicon, translated x86 processes can fail during ASan
startup, including with an empty program. Use a native x86-64 Linux runner for
full sanitizer results. A limited local run is available with
`python3 tests/run.py host --sanitizers undefined`; it keeps UBSan and assertions
but cannot detect all memory errors. Its output is separate, in
`tests/out/host-ubsan/`. CI always uses both address and undefined-behavior
sanitizers; there is no automatic downgrade.

## What the host tests exercise

| Area | Checks |
| --- | --- |
| Memory and strings | Byte-fill semantics, unsigned comparisons, zero length, alignment, destination guards, termination, copy return values, integer formatting, split empty fields/ownership and allocation-failure cleanup |
| Bitmap and vector | Bit boundaries, full bitmap, first free bit, growth, erase, independent copies, self-assignment |
| Memory setup | Page indices and structure sizes, BIOS/EFI map interpretation, sparse/high addresses, map instance isolation, early bump allocation and alignment |
| Framebuffer | Pixels, rectangle clipping, pitch, colour fills, cursor background restoration |
| VFS and registration | Volume selection, path translation, missing files/volumes, invalid indices/handles, create/read/write/close dispatch, error propagation, slot reuse |
| FAT32 | Small, empty, long-name and cross-cluster files; seeked reads; enumeration; rewrite/create/delete persistence; partial writes, EOF, empty-file growth, zero-fill, directory extension, invalid BPBs and cyclic chains |
| ELF | Identity, malformed/truncated headers, program-header bounds, filesystem loading, segment copying and BSS zeroing |
| User address spaces | Private frames, supervisor kernel mappings, W/X rejection, checked cross-page copies in both directions, read-only output rejection, bounded strings, holes/overflow, allocation failure and reclamation with an explicit physical allocator adapter |
| Keyboard and line input | Make/break bounds, Shift/Caps Lock, punctuation, extended/Pause sequences, FIFO wrap/overflow, left/right Ctrl-C through a full queue, arrows, editing and command-length limits |
| History | Fixed 256-entry wraparound, full-length commands, draft restoration, immutable recalled entries, duplicate retention |
| Argument parsing | Quoted/empty/escaped words, concatenated quotes, explicit background marker, capacity and malformed syntax |
| ABI | Fixed-width syscall record layouts and interrupt-frame offsets; historical `ActiveFile` layout checks remain for the unlinked legacy sources |

Memory/string functions are required to follow standard C semantics. A
nonstandard `memset`, `memcmp`, `strncmp`, or `strcpy` is a failure, even if
existing callers depend on that behavior. The VFS has its own documented
status-code interface; its `fread`/`fwrite` tests check that interface.

The small C++ harness uses registered functions and an always-enabled `CHECK`
macro; it needs no third-party framework. Python runs each case in a separate
process with a 15-second timeout. A crash, failed assertion, or sanitizer report
fails that case while later cases continue. Use `--case` for debugging, not as
a substitute for a full CI run.

Kernel implementations are linked directly from `kernel/src`. Shared sources
are linked from `common/src`; memory/string C++ and NASM primitives are compiled
separately for kernel and libc symbols. The same cases exercise both variants
and the independent BIOS stage-2 C primitives. Split cases cover all three
allocation adapters, plus injected failures at every shared-splitter allocation.
Test-only symbol
renaming prevents kernel memory/string/file functions from replacing the host
runtime's functions. NASM string/memory helpers are assembled and linked as
well. The ELF test includes the implementation to reach its internal validator.
Allocation, physical-address translation, page mapping, logging, RTC access,
and scheduling are explicit adapters. These tests do **not** exercise the real
heap, page-table manager, scheduler, port I/O, or interrupt assembly. Leak
checking is disabled because these adapters do not reproduce kernel ownership.

FAT32 tests use the real filesystem and driver over a bounded memory-backed
`Device`. mtools creates fresh images containing deterministic fixtures. Each
test loads its own copy. Mutation tests use the driver's normal write and close
paths, then inspect the saved image with mtools, including a surviving file
when checking deletion. A byte match in the live driver cache is insufficient.

## Reports and coverage

Each suite writes `results.json`, `junit.xml`, and individual case logs. Host
output also includes compiler/linker logs, tool versions, `coverage-summary.txt`,
`coverage.json`, `coverage.lcov`, and LLVM raw/merged profiles. Build and boot
failures have separate reports.

Coverage measures only production C/C++ sources linked into the host test binary.
It excludes test adapters and hosted headers. It is **not a whole-OS coverage
percentage**: NASM, other kernel translation units, the rest of BIOS stage 2,
UEFI, and real hardware behavior are outside that denominator. A process terminated before
flushing its profile can undercount executed lines. There is no percentage gate;
assertions and sanitizers are the correctness checks.

## Image and boot tests

The cheap asset suite assembles the actual MBR and stage 1, checks their lengths
and signatures, checks partition offsets/extents, validates the font, and checks
the retained userspace ELF's headers and executable entry segment.

`build_images.py` copies tracked and untracked source inputs into an isolated
directory, selects the cross toolchain, and builds each component independently.
It uses production Makefile dependencies and directory creation. Another
component can still report useful diagnostics after a failure.

When prerequisites pass, it checks ELF structure and BIOS payload sizes, invokes
the production image packager, and verifies fixed-LBA bytes and FAT contents.
UEFI checks include the PE machine and `EFI/BOOT/BOOTX64.EFI`. Missing fresh
images are reported as **blocked** with a failing exit status.

The emulator checks cover different layers:

- `python3 tests/run.py bios-chain` boots the real MBR and stage 1, which must
  reach a test-only stage-2 payload at `0x8000`. That payload writes to QEMU's
  debug-exit device. Only exit status 33 passes. This covers the early BIOS
  chain, not the production stage 2 or kernel.
- `python3 tests/qemu_boot.py --firmware bios` (or `uefi`) loads the freshly
  built image and debug ELF. GDB hardware breakpoints must reach `kernel_main`,
  `InitialiseIRQ()`, then `timer_callback`. It resumes through three more
  callbacks and checks increasing ticks, covering interrupt return and repeated
  delivery. The stopped program counter is checked at each milestone. A 60-second timeout fails the test. Reaching the
  IRQ initializer alone is not proof that a timer interrupt arrived.

The kernel boot runner uses TCG, the existing q35/USB topology, a disk snapshot,
and a private GDB Unix socket. UEFI gets a fresh OVMF variable-store copy. Logs
include debugger output and guest errors/resets. No physical disk or privileged
container is involved. This first boot test observes progress; it does not
prove filesystem persistence, graphics correctness, complete register-state
preservation, or exception recovery in the guest.

`python3 tests/qemu_boot.py --self-test` checks the debugger orchestration with
a tiny synthetic payload. It also verifies that a guest stuck after the first milestone fails on timeout.
Its labels imitate kernel milestones; its report is
explicitly separate under `qemu-harness`. It is not evidence of kernel boot or
IRQ delivery.

## Interactive shell and task regressions

`python3 tests/qemu_shell.py --firmware bios` (or `uefi`) starts the actual image
with a private QMP socket and sends QEMU keyboard events. It checks editing,
literal percent signs, overlong-command rejection, directory changes, file reads,
repeated close, task listing and the monotonic clock. Console characters are
captured through port `0xe9`; a framebuffer screenshot is saved as `screen.ppm`.

GDB first stops the shell in the syscall dispatcher and checks its saved CS/SS,
private CR3 and protected-session-shell flag after setup/login. This verifies actual automatic CPL3
execution before keyboard commands begin. The runner builds guest programs
against the active libc and copies them into a disposable image. The ABI probe
checks read-only file metadata/read/EOF, malformed and read-only output pointers,
unterminated paths, stale/foreign handles, the eight-handle limit, rejected legacy
calls, and denied power/input/kill privileges for ordinary programs. It leaves
handles open deliberately so exit cleanup can be checked.

Its assembly routine checks general registers, XMM0–15, x87 data/control state,
MXCSR, CF and DF across timer preemption. The safe clock syscall verifies timer
progress; the isolation suite independently checks per-task preemption counters.
Three sequential launches test return-through-libc exit and reuse. A fourth runs
alongside a spinning task, which the shell then kills while it owns an open file.
The shell's own open handle stays isolated throughout. Missing programs are
rejected before task creation. Idle and shell are protected from `kill`.

A held argument probe verifies `argc`, `argv[0]`, a null vector terminator,
private stack strings, quoted/empty/escaped arguments and a literal ampersand.
While the child spins, the harness queues keyboard input and asserts no prompt
or input echo appears. GDB verifies that the parent is in `WAITING` for that
child, then releases the child's private gate. Exit status 7 must precede the
next prompt. Queued child input is discarded at ownership return rather than
being executed as shell commands. A second program checks invalid argv/status
pointers, excessive argument sizes, non-child and repeated waits, collection
after idle cleanup, copied argument independence, fault status, legacy spawn,
and orphan cleanup. Existing concurrency probes use explicit `&` launches.

Keyboard tests recall multiple commands, restore a draft, re-execute recalled
text, cancel partial input with Ctrl-C, and verify original quoted commands in
`history`. The packaged INPUT program receives edited text and returns normally;
another copy is interrupted during input. Two busy probes that never read input
are terminated with Ctrl-C, once with a deliberately full queue. Background input
and backspace are denied, and a background task survives those interrupts.
The probes hold open files to verify Ctrl-C cleanup. GDB checks terminal ownership
returns to the shell with no pending interrupt.

GDB checks that all seventeen terminated tasks were reaped, only idle and the isolated
shell remain, user-page accounting matches the shell's live ownership, and no
file handles leaked. Finally the harness types `shutdown` and requires QEMU to
exit successfully with a QMP `SHUTDOWN` event whose reason is `guest-shutdown`.
A log message alone cannot pass this check. No debug-exit or monitor quit is used
to satisfy the power-off assertion.

Reports, console/debugger logs, QMP events, guest binaries, the disposable image
and screenshot live in `tests/out/qemu-shell-bios/` or `qemu-shell-uefi/`.
Each awaited milestone has a timeout. Source images and firmware variables remain
untouched. These tests do not establish physical-PC compatibility.

## Isolated ring-3 regressions

`python3 tests/qemu_user.py --firmware bios` (or `uefi`) builds probes against
the public syscall definitions and shared assembly stub, installs them in a
copy of the fresh image, and runs the same q35 topology. It checks the bundled
demo's print/exit path and runs two simultaneous copies of a probe at identical
virtual addresses. GDB verifies distinct CR3 roots and code/data/stack frames,
supervisor kernel mappings, text and stack permissions, stack guards, saved
CPL3 frames, repeated timer preemption, and private data. It releases both probes
through their separate physical backing pages; both must finish successfully.
The assembly probe checks GPR, x87, XMM0–15, MXCSR and flag preservation.

Guest checks exercise valid cross-page console writes, bad pointer ranges,
unmapped pages, excessive lengths, and rejection of every legacy syscall.
Nine separate tasks attempt kernel reads/writes, text writes, stack execution,
port I/O, CLI, kernel-only software interrupts, disabled SYSCALL, and stack-guard
access. Each must fault with the expected exception while the shell survives.
Malformed ELF policies are rejected before task creation. Finally, GDB verifies
all twelve test-task stacks were reaped and only the login agent and shell's private
frames/tables remain.
Reports, console/GDB logs and a framebuffer screenshot are saved under
`tests/out/qemu-user-bios/` or `qemu-user-uefi/`. These checks cover QEMU's
configured CPU and firmware; they do not establish physical-PC compatibility.

## GitHub Actions

[The workflow](../../.github/workflows/tests.yml) runs on pushes, pull requests,
and manual dispatch. Three independent jobs run host tests, asset checks, and
the BIOS-chain probe. Two further jobs build and boot BIOS and UEFI with matrix
fail-fast disabled. The firmware jobs also exercise the public parallel build
command, check the Compose configuration, and run the interactive shell/task
and ring-3 regressions for each firmware. A failed host test does not
suppress firmware diagnostics.

Containers use a digest-pinned Ubuntu base, Clang/LLVM 18, and source-pinned GCC
15.2.0/binutils 2.47 with verified SHA-256 downloads. Ubuntu packages receive the
repository's updates; this is not a bit-for-bit lock of every dependency. Buildx
caches the tool images. The cross compiler's initial source build is slower than
later cached runs. Tests run without network access, with the checkout mounted
read-only and only `tests/out` writable.

All jobs upload available logs, JUnit/JSON reports, coverage, images, and debug
ELFs even after failures, retaining them for 14 days. The workflow requires no
publishing credentials and publishes no container image. Once these files are
committed and pushed to GitHub with Actions enabled, the workflow runs normally.
Failures are not marked expected or exempted.

## Next coverage to add

Extend malformed LFN records and allocation-failure cases, and add real heap
split/coalesce tests, allocation failure injection, paging ownership, and shared
boot-info layout checks. Keep intended behavior explicit for APIs whose EOF,
partial-I/O, or failure contracts are still undefined.

Extend guest tests with disk-write persistence, controlled framebuffer scenes,
user resource exhaustion and future handle-based APIs. Keep testing isolation
and syscall pointer validation as the userspace boundary grows.

### Userspace windows

After fresh image builds, run `python3 tests/qemu_windows.py --firmware bios`
and `--firmware uefi` (or `make -C tests test-windows FIRMWARE=...`). The suite
launches the packaged `WINDOW` demo from the ring-3 shell, checks real framebuffer
pixels and console restoration, and captures `window.ppm` / `closed.ppm`.
It exercises Q, PS/2 mouse closure and Ctrl-C; a guest probe checks record/pixel
pointers, rectangle limits, per-process quota, foreign and stale handles. GDB
then verifies cleanup after normal exit, fault and kill, including user page
reclamation. Diagnostics live in `tests/out/qemu-windows-{bios,uefi}`; CI runs
this suite for both firmware paths.

The window suite also drives real PS/2 dragging, min/max resize and screen-edge
clamping, pointer capture with an outside release, focus/z-order across two
independent owners, key isolation, a background Ctrl-C, and full event-queue
recovery. GDB checks geometry, focus, pixel changes, queue bounds and complete
resource reclamation. `interactive.ppm` captures the resized, painted demo.
Host tests cover queue FIFO/wrap/overflow/reset and the new 32-byte ABI records.

The suite also verifies title-bar maximize/minimize, bottom-bar restore, saved
normal geometry, minimized focus fallback, signed PS/2 wheel steps and routing
to an unfocused hovered owner without stealing focus. `maximized.ppm`,
`minimized.ppm` and `normal-restored.ppm` capture those states. Pixel assertions
check the far side of the maximized client; guest probes reject invalid/foreign/
stale mode calls and exercise minimized-owner exit, fault and kill cleanup.
Host tests exercise the three-byte fallback and four-byte wheel decoder.

Full-screen demo repaints now use the ordinary 60-second milestone timeout.
The suite keeps sending mouse motion while waiting for the maximized repaint,
checks its far-side pixels and the large normal-size gradient, and records the
maximized transition/repaint time in `maximized-seconds.txt`. This is an end-to-end
TCG measurement including event dispatch, not a hardware frame-rate guarantee.
Host tests cover clipped row copies with different source/destination pitches
and padding/guard preservation.

The step-9 q35/TCG runs measured about 1.5 seconds (BIOS) and 1.6 seconds
(UEFI) for maximize plus full repaint while mouse motion continued. The step-8
path exceeded 60 seconds before completion. Host load affects these timings;
pixel correctness, input progress and eventual final-frame flush are asserted
separately.

The step-10 shell suite checks built-in argument dispatch (`ps aux`), quoted
command names/paths, whitespace, empty/escaped arguments, print joining, malformed
syntax and count limits. Invalid power/file/process arguments must produce usage
without performing their operation. It also checks the full 16-argument program
limit through `user` and preserves the existing launch, history and wait checks.

Step 11 packages and checks eleven command executables in both images. Shell
regressions verify external PS execution, directory inheritance, command lookup
from another directory, absolute/relative paths, file-reader cleanup and error
statuses. The ABI probe checks getcwd/chdir pointer validation, child-directory
isolation and denied power calls (including an unprivileged child launching
SHUTDOWN). Cleanup compares reaped tasks with all successful launches. Window
regressions launch `window` by name, including background owners.

Step 12 checks the graphical console position after `clear` and `print` to prove
that start/exit boilerplate no longer consumes console lines; debug-port traces
remain available for lifecycle assertions. `quiet-console.ppm` records the view.
The shell suite installs disposable normal, dot-prefixed, hidden and read-only
files, then exercises `ls`, `-l`, `-a`, combined/separate flags, option termination,
metadata, row layout and invalid arguments. The style-only pass was audited for
unchanged non-brace tokens, accounting for preprocessor continuations and adjacent
string literals; host and both firmware suites validate the result.


Step 13 adds host tests of the production account registry: fixed IDs, valid and
invalid names, duplicate/capacity rejection, full-width IDs, supplementary-group
limits, idempotence and independent credential snapshots. The shell suite builds
and installs CREDPROBE only into its disposable image. It tests root and non-root
syscalls directly, invalid output/input pointers, unknown identities/flags,
ordinary and grandchild inheritance, immutable process snapshots after registry
updates, root-group membership without UID-0 privilege, same-UID and cross-UID
kill policy, protected tasks, nested terminal return (including an exited
launcher), background launch fallback and failed identity
selection. Command checks cover the new identity/account tools, `runas`, denied
shutdown/restart and account changes, plus interactive non-root input and Ctrl-C.
The ABI probe now runs its ordinary permission tests under UID 1000, selected
through the real root-only launch API. Existing lifecycle checks count all new
tasks and require complete reclamation. The final root SHUTDOWN command still
must produce a real QEMU ACPI shutdown event on both firmware paths.


Step-13 verification on 2026-10-27 passed 151 host checks with the documented
UBSan fallback, five boot-asset checks, the BIOS-chain probe, fresh builds and
all boot/shell/ring-3/window suites on BIOS and UEFI. The local translated ASan
runtime failed the empty-program allocator preflight; full ASan remains a CI
requirement. The BIOS stripped kernel is 128,984 bytes within the unchanged
131,072-byte slot. ELF headers/load segments and both framebuffer captures were
also checked. The touched account/dispatcher files use `-Os`; frame pointers,
freestanding flags and interrupt ABI are preserved.


## Persistent account and login regressions

`python3 tests/qemu_accounts.py --firmware bios` (or `uefi`), also exposed as
`make -C tests test-accounts FIRMWARE=bios`, uses a writable **disposable image**
and copied OVMF VARS. It performs real PS/2 first-boot setup, rejects short,
mismatched and invalid credentials, verifies hidden password input, tests wrong,
unknown and locked logins, and authenticates root and non-root sessions. It
creates users/groups/memberships, sets and changes passwords, stops QEMU and
boots the same image to check persistence. Python independently checks the slot
SHA-256 and full-cost PBKDF2 result. Repackaging must preserve exact slot bytes.

The AUTHPROBE guest verifies that neither root nor non-root ordinary programs
can call login/setup APIs or open account payloads, and checks root password
copy bounds and non-root privilege rejection. Logout ends background processes
under multiple UIDs and an open window; GDB verifies only idle/login remain,
terminal ownership returns, and private-page/window counts match.

Separate boots damage the newest generation (recover the older one), both
slots (disable login), the first setup (disable login), or remove a slot
(disable login). A CPU without RDRAND refuses setup without writing credentials.
QEMU blkdebug injects write errors to check explicit failure and frozen login.
No production image or firmware template is modified by these tests.

Host tests cover SHA-256 and PBKDF2 vectors including long HMAC keys, account
schema validation, alternating generations, sector-boundary torn writes,
read-back corruption and flush failures. FAT fixtures check separate bounded
slot mappings, duplicate chains, wrong file sizes and propagated read errors.
Ordinary shell, isolated-program and window suites now perform setup/login first;
the boot milestone test intentionally stops before authentication.

Results, console logs, debugger state and setup/login screenshots are under
`tests/out/qemu-accounts-{bios,uefi}`. Disposable test images contain published
test credentials only. Native x86-64 CI retains ASan+UBSan; local translated
ASan is incompatible on this host, so local host validation uses the documented
UBSan fallback.

Step 14 local validation passed 163 UBSan host checks, five boot-asset checks,
the BIOS loader-chain probe, fresh BIOS/UEFI builds, and both firmware boot,
account/login, shell, isolated-process and window suites. The stripped kernel is
161,352 bytes in the 393,216-byte BIOS reservation. A real preserving rebuild
retained both account slots byte for byte; a missing store aborted without
replacing the original image. Formatting and Compose configuration checks passed.

The account suite also checks numeric and named logged-out restart/shutdown
actions using QMP guest power events and a second boot into the menu after
restart, rejects unknown menu actions, and shuts
down after logout. These controls work before setup without modifying the empty
store; ordinary non-root power calls remain denied by AUTHPROBE.

## File utilities and editor regressions

`python3 tests/qemu_files.py --firmware bios` (or `uefi`), also available through
`make -C tests test-files FIRMWARE=bios`, uses a writable disposable image and
fresh copied OVMF VARS. It tests the standalone utilities, hex/binary output,
32 KiB limits, empty/binary/read-only files, invalid paths, destination conflicts,
protected account paths, invalid buffers/flags, closed and cross-process open
handles, and cleanup after exit. Real PS/2 input exercises multiline editing,
arrows, Delete, Ctrl+S and Ctrl+X. Independent mtools reads and another guest boot
check exact saved bytes, truncation, rename and deletion. QEMU blkdebug injects
real write failures; the editor must report the failed save, keep its buffer,
and return to the shell. Console logs, GDB cleanup checks and framebuffer captures
are stored in `tests/out/qemu-files-{bios,uefi}`.

Host tests use the production text buffer and keyboard decoder, remount mutated
FAT images, inspect them independently with mtools, and inject read/write/flush
failures, including directory-capacity failures after staging a save. Staged
clusters must be released when publication cannot start. Save failures at
successive write phases must leave the original or
complete replacement payload, never a partial buffer. These checks do not claim
power-loss atomicity or journal recovery.

Step 15 local validation passed 171 UBSan host checks, five boot-asset checks,
the BIOS loader-chain probe, fresh BIOS/UEFI builds and boot, account/login,
shell/credentials, isolated-process, window and file/editor suites on both
firmware paths. The final file suite also saves as an ordinary non-root user and
checks every byte of a 32 KiB file independently. Both editor framebuffer captures
and ELF headers/load segments were inspected. The stripped BIOS kernel is
171,528 bytes within the unchanged 393,216-byte reservation. Formatting and
workflow lint checks passed. Full ASan remains a native x86-64 CI requirement.


## Storage hardening and permission regressions

`python3 tests/qemu_storage.py --firmware bios` (or `uefi`), also exposed as
`make -C tests test-storage FIRMWARE=bios`, uses disposable disks and copied
OVMF variables. STORPROBE checks owner/group/other access, directory traversal,
execution rejection, mode/ownership changes, protected files, exclusive writers,
stale/foreign handles, handle-table exhaustion, invalid buffers/flags, gap zeroing and
rebooted bytes/metadata. The harness uses GDB to stop after a prepared rename
journal is durable, kills QEMU, and boots again to verify replay and a single
surviving name. A full-volume fixture checks editor failure with its buffer
retained and existing data intact. Screenshots and independent mtools reads
accompany both firmware reports under `tests/out/qemu-storage-{bios,uefi}`.

Host checks cut every sector boundary of save/create/rename transactions, fail
each flush barrier, reject corrupt metadata/journals and device extents, exhaust
the journal overlay, check metadata ownership across rename/save/delete/reuse,
and exercise VFS access modes and stale/arbitrary tokens. The full-directory
fixture creates valid permission records before exhausting its allocation space.
Malformed metadata must not be mistaken for an ordinary full-volume failure.
Guest fixture import helpers add records only to clean disposable test images;
production used volumes must not be edited by external tools.

The step-16 file-info ABI is 288 bytes; all guest programs are rebuilt. The
non-root editor test now writes under `/tmp`, since root-owned `/` is 0755.
The shell listing tests check actual mode/UID/GID alongside FAT attributes.

Step 16 local validation passed 180 UBSan host checks, five boot-asset checks,
the BIOS loader-chain probe, fresh BIOS/UEFI builds and all seven QEMU suites
(boot, storage, accounts, shell, isolated programs, windows and file/editor) on
both firmware paths. The stripped kernel is 192,240 bytes in the unchanged
393,216-byte BIOS slot. ELF headers/load segments and storage/editor framebuffer
captures were inspected. Both account slots survived an old-format preserving
rebuild byte for byte. Formatting (18.1.8), workflow lint and Compose checks
passed. The final report set is `tests/out/step16-final/validation.json`; full
ASan+UBSan remains required on native x86-64 CI.
