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
make -C tests test-usability FIRMWARE=bios
make -C tests build-images FIRMWARE=uefi
make -C tests test-qemu FIRMWARE=uefi
make -C tests test-shell FIRMWARE=uefi
make -C tests test-user FIRMWARE=uefi
make -C tests test-usability FIRMWARE=uefi
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
command names/paths, whitespace, empty/escaped arguments, echo joining, malformed
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

Step 12 checks the graphical console position after `clear` and `echo` to prove
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

The file-info ABI is 296 bytes (including creation/modification timestamps); all guest programs are rebuilt. The
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


## Userspace usability regressions

`python3 tests/qemu_usability.py --firmware bios` (or `uefi`), also available
through `make -C tests test-usability FIRMWARE=...`, installs USABPROBE only into
a disposable image. Real PS/2 input exercises command/path completion, quoted
filenames, blocked reads, Ctrl-Z, `jobs`, `fg`, `bg`, `wait` and group termination.
GDB checks the shell/reader's blocked states, stopped groups with multiple members,
restored terminal ownership, interruptible background waits, distinct private heap
frames, NX permissions and cleanup after exit, fault, Ctrl-C, group kill and
logout. A leader exits before its child to verify that group waiting survives
idle reclamation. Nested shells and
root/non-root controllers exercise job ownership, invalid flags, bad/read-only
status pointers, polling and consumed results. Existing CPL3 entry checks now
wake the blocked shell with an empty Backspace after the debugger resumes. They
inspect the saved frame through the SysV entry argument at an exact instruction
breakpoint, avoiding optimized-variable locations in the function prologue. The
file-handle fixture runs in the background because it intentionally leaves an
orphan reader alive; foreground group waits now correctly include that reader.

The runtime probe tests heap limits, zeroing after shrink/regrowth, invalid sizes,
16-byte alignment, calloc overflow, realloc data preservation/failure, exhaustion,
coalescing, new/delete, split cleanup and bounded/integer formatting. Executing a
heap page must fault. Host tests run the production allocator/completion parser
and address-space heap mapping with explicit growth/frame adapters, including
allocation rollback and guards. Reports and framebuffer captures live under
`tests/out/qemu-usability-{bios,uefi}/`; CI runs both firmware paths.

Step 17 local validation passed 185 UBSan host checks, five boot-asset checks,
the BIOS loader-chain probe, fresh BIOS/UEFI builds and all eight QEMU suites
(boot, usability, storage, accounts, shell, isolated programs, windows and
file/editor) on both firmware paths. The stripped kernel is 198,672 bytes in
the unchanged 393,216-byte BIOS slot. ELF headers/load segments and usability
framebuffer captures were inspected. Both public parallel build paths passed,
as did formatting (18.1.8), workflow lint and Compose checks. The final report
set is `tests/out/step17-verified/validation.json`; full ASan+UBSan remains
required on native x86-64 CI.

## Stream, pipeline, timeout and serial regressions

`python3 tests/qemu_streams.py --firmware bios` (and `uefi`), or
`make -C tests test-streams FIRMWARE=...`, installs STREAMPROBE on a disposable
image. It exercises descriptor inheritance and shared offsets, duplication,
exhaustion, invalid buffers, atomic full-pipe writes, blocking wakeups, EOF,
broken pipes, poll events, deadline expiry without consuming results, held-job
startup and cleanup. Real PS/2 commands test quoted/adjacent redirections,
append/truncate, left-to-right stderr duplication, byte-exact binary pipelines
larger than the buffer, built-in redirection, stopped/background jobs, Ctrl-C,
kill and logout. GDB observes blocked/stopped states and resource counts;
mtools independently checks persisted bytes. COM1 logs must include loader and
kernel entry, runtime output and a controlled user fault without passwords. A
second boot with no UART verifies login and saved output remain usable.

Host parser tests cover operator quoting/escaping, ordered redirects, malformed
syntax and bounds. Kernel boot tests compare every PT_LOAD byte and BSS before
entry, catching BIOS staging/page-table overlap as the kernel grows. Existing
account, shell, userspace, window, file, storage and usability suites remain
required on both firmware paths. CI includes the new streams suite and artifacts
under `tests/out/qemu-streams-{bios,uefi}/`.

Step 18 local validation passed 191 UBSan host checks, five boot-asset checks,
the BIOS loader-chain probe, two debugger-harness checks, fresh BIOS/UEFI builds
and all nine QEMU suites on both firmware paths. The stripped kernel is 215,352
bytes in the unchanged 393,216-byte BIOS slot. Both loaded ELF segments and BSS
were checked against the build; stream framebuffer captures were inspected.
Formatting (288 project-owned C/C++ files, clang-format 18.1.8), workflow lint
and Compose checks passed. Legacy `cat`/`read` usage assertions now reflect stdin
support; job-control tests synchronize on observed scheduler state and command
prompts, and pipeline output checks exclude debug-only lifecycle messages.
The final report and source hashes are under `tests/out/step18-verified/`.
Full ASan+UBSan remains required on native x86-64 CI.

## Clock and hardware utility regressions

`python3 tests/qemu_utilities.py --firmware bios` (and `uefi`), or
`make -C tests test-utilities FIRMWARE=...`, installs UTILPROBE into a disposable
image. It validates `echo` output byte for byte, each clock flag and combinations,
every hardware section, root denial through both the utility and direct syscalls,
invalid output pointers, out-of-range indices, PCI addresses/controller counts,
and report pipelines/redirection. mtools verifies saved files independently;
framebuffer captures and a full text report live in `tests/out/qemu-utilities-*`.
CI runs it on both firmware paths alongside the existing suites.

Host RTC tests cover BCD/binary modes, 12/24-hour conversion, midnight/noon,
century/leap-year validation, year rollover during sampling, and bounded failure
when the update-in-progress bit stays set. FAT keeps its internal `tm` conventions
(years since 1900, zero-based month); the public clock uses calendar values.

Utility validation passed 194 UBSan host checks, five boot-asset checks, the BIOS
loader-chain probe, fresh BIOS/UEFI builds and all ten QEMU suites on both
firmware paths. The stripped kernel is 220,208 bytes in the 393,216-byte BIOS
slot. ELF load segments/BSS and utility framebuffer captures were checked.
Formatting (clang-format 18.1.8) and workflow lint passed. Final results, sample
hardware reports and source hashes are under `tests/out/utilities-verified/`.
Full ASan+UBSan validation remains a native x86-64 CI requirement.

## Stream filter and pager regressions

`python3 tests/filter_commands.py` compiles the real grep/wc/head/tail/tee/more/sort/uniq/cut/tr
sources and libc output/string routines, substituting only hosted syscall
transport. It runs with ASan+UBSan in CI (`--sanitizers undefined` is the explicit
local fallback). It compares hundreds of deterministic binary streams and
patterns against independent byte/regex expectations, exercises EOF and partial
lines, 64/128 KiB boundaries, invalid options, short reads/writes, broken pipes,
open/read/write/close failures, tee append/multiple outputs, and paging keys and
wrapping. `make -C tests test-filter-commands` is the equivalent entry point.

`python3 tests/qemu_filters.py --firmware bios` (and `uefi`), or
`make -C tests test-filters FIRMWARE=...`, boots a disposable image, provisions a
normal guest login, and runs the six original tools under that identity. It verifies
four-stage pipes, saved output with mtools, permission denials, early consumer
exit, redirected/background more, Space/Enter/q, Ctrl-Z/fg, and paging while the
upstream producer remains blocked on a full pipe. FILTERPROBE validates copied
terminal-info records, bad descriptors/destinations, background read denial and
non-inheritance of input access by an ordinary child of a pipeline stage.
GDB synchronizes pager resume with its blocked-input state and verifies that
quitting leaves no live pipe, stream or file handles.
Both firmware jobs run this alongside the existing stream/job-control suites.
Reports and screenshots are in `tests/out/qemu-filters-*`; hosted results are in
`tests/out/filter-commands*`.

The hosted filter suite also covers sort/uniq/cut/tr: byte and exact decimal ordering,
unique comparison keys, adjacent groups, field/byte ranges, ASCII classes,
translation/deletion/squeezing, malformed arguments, buffer limits, short I/O
and broken pipes. Deterministic binary-stream comparisons exercise the real
command sources. `python3 tests/qemu_text_tools.py --firmware bios` (and `uefi`),
or `make -C tests test-text-tools FIRMWARE=...`, runs the four new tools under
a normal-user login and verifies four-stage pipelines, file/input/output operands, append/background
redirection, permission denials, early consumers and repeated jobs on BIOS
and UEFI. Both firmware CI jobs run this suite; reports are in
`tests/out/qemu-text-tools-*`. This suite uses QMP over subprocess pipes without
a debugger socket. Linux retains LeakSanitizer; macOS disables only
unsupported leak detection while retaining ASan and UBSan.

The original six-command filter validation passed all eight hosted command groups with ASan+UBSan on the
native Linux runtime, 194 existing UBSan host checks, five boot-asset checks,
the BIOS loader-chain probe, fresh BIOS/UEFI image builds and all eleven QEMU
suites on each firmware path. The stripped kernel is 220,432 bytes within the
unchanged 393,216-byte BIOS slot. The image checks validate every command's ELF
page layout; pager screenshots were inspected. Formatting and workflow lint
passed. After the final pager prompt-clearing adjustment, rebuilt images passed
the focused filter suites again on both paths; the kernel and 34 other command
binaries were verified identical to the full-regression run. Results and source
hashes are saved in `tests/out/filters-verified/`.
The existing x86-64 host suite still requires ASan+UBSan in native x86-64 CI;
its local UBSan fallback is separate from the sanitized command checks.

## Terminal appearance regressions

`python3 tests/qemu_terminal.py --firmware bios` (and `--firmware uefi`) uses a
disposable image to check the terminal getter/setter ABI, invalid destinations,
atomic rejection, ordinary-user access, background and non-foreground child
denial, CLI validation and exit codes, reset, pipelines and redirected values.
It compares actual screenshot pixels at 8, 30 and 32 pixel heights to the bundled
PSF font, including tabs and RGBA compositing. The host console tests cover every
even size from 8 through 64, clipping/unsigned glyph indices, padded scanlines,
whole-row scrolling, geometry, alpha and reset. The existing shell, windows and
filter suites cover input/foreground handoff, window chrome and paging regressions.

Terminal validation passed 198 UBSan host checks, 13 isolated framebuffer/console
checks with native Linux ASan+UBSan, fresh BIOS/UEFI builds, and the terminal,
shell, windows, filters, streams and usability QEMU suites on both firmware paths.
Screenshot pixels and previews were checked. The BIOS kernel is 227,552 bytes
within its 393,216-byte slot. Formatting, Python syntax and workflow lint passed.
Source hashes are in `tests/out/terminal-build-manifest.json`; guest suite results
are in `tests/out/terminal-{bios,uefi}-validation.json`.

## Login performance checks

`python3 tests/qemu_login_performance.py --firmware bios` (or `uefi`) creates a
disposable account image and measures three real logins. Set `GDB` to the target
debugger as for the other guest suites. It reports Enter-to-shell wall time and
time inside production `Pbkdf2Sha256` separately in
`tests/out/qemu-login-performance-{bios,uefi}/timings.json`. The remainder includes
QMP polling, debugger detach and shell startup; these are diagnostic timings, not
a machine-independent performance assertion. Use an otherwise idle host for
comparisons; `--samples` selects 1–20 logins.

The crypto host tests include independent PBKDF2 vectors at the 64-byte HMAC key
boundary, embedded NULs, empty inputs and salt lengths spanning SHA-256 padding
and block boundaries. The account suite independently verifies the full
600,000-iteration stored digest and exercises existing accounts after reboot.

Login optimization validation passed 199 host checks with UBSan, 100 independent
crypto comparisons (including three full-cost hashes) with native Linux
ASan+UBSan, five boot-asset checks, fresh BIOS/UEFI builds and all six account-suite
checks on each firmware path. Both new images also authenticated an account
created by the old implementation, leaving both account slots byte-identical.
Five debugger-free samples per case on the local
loaded host had median Enter-to-shell times of 7.35 → 3.58 seconds for BIOS and
7.86 → 3.27 seconds for UEFI; these are workload-dependent measurements, not
latency guarantees. Separate debugger samples attributed most login time to
hashing. Timing data and focused checks are under `tests/out/login-performance/`;
the full x86-64 host suite still requires ASan+UBSan in native x86-64 CI.

## Shared boot-information ABI

Every loader/kernel build compiles the assertions in `include/hackos/boot_info.h`
in its own C or C++ environment. The image/asset suite also compiles both
languages for x86-64 and requires packed and 32-bit builds to fail assertions
(`BOOT_LAYOUT_CC` may select a Clang executable).
`tests/qemu_boot.py --firmware bios` and
`--firmware uefi` independently decode the 64-byte handoff at kernel entry,
validate pointed-to font/map/ACPI data and framebuffer geometry, then require
IRQ initialization and repeated timer interrupts. Run these after fresh
`tests/build_images.py` builds; results live in `tests/out/qemu-{bios,uefi}`.

## Sustained desktop and framebuffer coverage

`make -C tests test-desktop-stress FIRMWARE=bios` (or `uefi`) runs eight measured
cycles plus one warmup on a disposable image. `STRESS_CYCLES=1..100` selects a
shorter or longer run. It uses real QMP keyboard/pointer input to launch a terminal,
Files, Settings and Editor together; runs a redirected pipeline; types while
dragging/resizing; checks resulting geometry; and injects ingress overflow while
a button/modifier is held. Capture, buttons and modifiers must reset. Cycles
alternate normal unsaved-document discard and forced editor termination. The
harness waits for resize events to drain and the close request to be delivered
before sending dialog navigation, rather than assuming fixed UI response times.

Every cycle fills all eight terminal endpoints, rejects a ninth controller, then
closes all applications. After reaping and painting settle, the following must
exactly match the warmed baseline: processes, windows, surface pages, user pages,
open files, streams, pipes, descriptors, file handles, allocated kernel heap bytes
and blocks, and total used physical memory. Warming permits initial caches/heap
capacity growth; subsequent growth fails. Results and elapsed time are saved in
`tests/out/qemu-desktop-stress-{bios,uefi}/stress-resources.json`, with GDB logs and
a final screenshot. This is a bounded soak workload, not a claim of exhaustive
hardware or concurrency coverage. Existing `qemu_windows.py` separately repeats
eight injected physical allocation failures before checking successful recovery.

Host tests compare 4096 clipped rectangle/copy operations against an independent
scalar oracle, including extreme unsigned coordinates, partial source copies,
unaligned origins, padded rows and untouched canaries. A presentation matrix
covers widths around scalar/SSE/block boundaries, differing pitches, primary
colours and reserved-byte patterns. A 100000-operation queue model checks FIFO,
wraparound, overflow acknowledgement and reset. These tests cover the supported
32-bit BGR framebuffer contract; RGB-order and 16/24-bit conversion are not added.

`make -C tests test-graphics-sanitizers` runs the 27 renderer/console/queue cases
under native ASan/UBSan (plus its build check). Set `GUI_CXX` to choose the native
compiler. On ARM this exercises the scalar renderer with explicit hosted adapters
for the x86 assembly byte-copy primitives; the full x86 host suite independently
exercises the real assembly and SSE paths. These adapters do not replace clipping,
comparison, drawing, console or queue logic.

Local validation on 2026-10-29 passed fresh BIOS/UEFI builds, both extended window
suites, 220 x86-64 UBSan host checks, and all 27 native ARM ASan/UBSan renderer,
console and queue cases. Each firmware completed eight measured desktop cycles
plus warmup: 273.609 seconds on BIOS and 468.873 seconds on UEFI, including test
and debugger overhead. These are workload durations, not performance comparisons.
Every completed cycle restored the exact warmed resource baseline. Final desktop
screenshots showed no leftover application pixels. The full style check passed
348 project-owned C/C++ files.

## Typing latency and retained-frame reliability

`make -C tests test-input-latency FIRMWARE=bios` (or `uefi`) measures three
individual character updates in graphical first-boot login and a desktop terminal
on a disposable image. It records QMP injection to GUI dispatch, dispatch to
matching window commit, and commit to presentation under
`tests/out/qemu-input-latency-{bios,uefi}/timings.json`. Guest PIT ticks and debugger
wall times are separate. Run sequentially without competing VMs; debugger stops,
host scheduling and TCG affect the samples. This does not measure noVNC transport
or browser paint, and sparse samples are not latency percentiles.

The 2026-10-29 local three-sample run found QMP-to-GUI dispatch around 2–4 ms,
while application repaint/commit dominated. BIOS median dispatch-to-presentation
wall times changed from 264 to 146 ms for login and 277 to 121 ms for terminal;
final UEFI medians were 182 and 98 ms respectively (no UEFI before sample).
These wall measurements include debugger overhead. Corresponding guest-clock
medians were BIOS 194→49 ticks for login and 132→75 for terminal, and final UEFI
72/68 ticks. The PIT increments at 1000 Hz, but guest ticks under TCG are not a
substitute for elapsed host time. Samples are exploratory, not latency guarantees;
the final harness also lets initial painting settle before its first key.
Baseline JSON is retained under `tests/out/input-latency-baseline/bios.json`.

The fix removes redundant terminal key-transition repaints and clipped glyph
work, and gives login bounded retained-frame updates instead of full maximized
client uploads. Its centered form/dialog region is repainted on edits; general
per-widget damage tracking remains a follow-up. Browser transport was not tuned
or measured by this test.

The shared GUI sanitizer suite compares full rendering with 1-, 7- and 16-row
tiles, including nonzero origins, padded rows, selection and modal overlays.
The adapter tests partial uploads, resize fallback and invalid damage. The window
suite checks retained pixels and independently injects eight physical surface
allocation failures (four retained-BEGIN/restore pairs) at the allocator entry, before mutation.
It verifies old pixels, geometry and allocation counts survive, then retries
successfully. The original quota-exhaustion checks remain. Window capacity and
stale-handle cleanup run for 32 create/fill/close cycles, followed by existing
exit/fault/kill reclamation checks. The GUI input suite separately exercises
window/ingress queue overflow and reset recovery.

Local validation passed fresh BIOS/UEFI builds and all five guest suites (windows,
GUI input, desktop, graphical login and editor) on both paths. The GUI ASan/UBSan
runner passed 24 groups including 5000 stress iterations. Of 217 x86-64 UBSan
host checks, 216 passed initially; the filesystem interruption/recovery stress
case exceeded its 15-second limit during concurrent builds/VMs, then passed alone
under the same limit. The style checker passed all 348 project-owned C/C++ files.

## Graphics presentation performance

`python3 tests/qemu_windows.py --firmware bios --profile` adds seven samples
for small-window, maximized-window and desktop frames. Scene samples change one
visible client pixel (or console pixel on the desktop), mark its bounds damaged
and request composition; cursor samples move the pointer without
changing scene content; full samples force an unconditional presentation.
`--profile-only` stops after the small/maximized samples, and
`--build-directory /absolute/path/to/bios` selects a saved image, debug ELF and
source tree for before/after comparisons. Run comparisons sequentially on an
otherwise idle host. JSON timings include debugger round trips and host scheduling;
they are not hardware FPS measurements or CI performance thresholds.

The profiling checks also assert that cursor-only frames do not increment scene
composition/presentation counters and that the old cursor footprint is restored.
The regular window suite covers drag/resize, overlapping windows, focus changes,
minimize/maximize/restore, uploads and owner cleanup. Host tests compare changed
span presentation against full-frame output over repeated scene changes. They
check clipped damage unions, partial views that preserve surrounding pixels,
clipped copies and distinct padded pitches. Deliberately different device-buffer
contents verify that unchanged RAM causes no device writes.
The SSE2 fast paths retain scalar tails; the kernel already saves SSE state on
interrupts. Native ARM sanitizer runs exercise the portable renderer path;
x86-64 host and guest checks exercise the SSE2 paths.

The 6a compositor additionally checks that a synthetic one-pixel scene update
composes exactly one pixel, cursor-only updates compose none, and forced full
updates compose the full screen. Profiling waits for app input/painting to settle
before injecting damage. At geometry, typing, minimize, restore, close and cleanup
milestones, the suite compares retained partial composition byte-for-byte with a
forced full recomposition. Host checks cover nonzero glyph clips, padded copies,
changed-frame bounds, damage-list overflow and repeated overlapping scene moves.

`input-latency.json` records text/drag/resize sample counts and total/last/max PIT
ticks from the same run. These start at GUI dispatch; geometry samples do not
wait for app resize repaint. They include frame pacing, use the oldest coalesced
action and have millisecond clock resolution. Read them as bounded-workload
diagnostics, not hardware latency guarantees. The `Display` counters and their
limitations are described in [graphics](../system/graphics.md#observing-graphics).

The 6a local run (2026-10-29, 1920x1080, seven samples per case) produced these
median debugger wall times. The baseline firmware runs overlapped; final runs
were sequential without other test VMs, so this is an exploratory comparison,
not a controlled speedup estimate. The exact region-area assertions above are
independent of host timing.

| Firmware | Scene | One-pixel before → after | Forced full before → after |
| --- | --- | ---: | ---: |
| BIOS | Small window | 12.60 → 2.46 ms | 15.32 → 14.55 ms |
| BIOS | Maximized | 18.91 → 2.52 ms | 22.44 → 16.67 ms |
| UEFI | Small window | 12.06 → 2.43 ms | 13.30 → 15.22 ms |
| UEFI | Maximized | 11.29 → 2.48 ms | 14.90 → 16.67 ms |

Full presentation remains screen-sized work; the measurements do not establish
an improvement there. Final cursor-only medians were 1.34–1.53 ms. Raw baseline
samples are in `tests/out/performance-baseline/`; final small/maximized samples
are in `tests/out/qemu-windows-{bios,uefi}/*-timings.json`.

The full interaction runs collected one sample per operation: BIOS text/drag/
resize took 26/4/3 ticks, UEFI 31/3/1 ticks (currently 1 ms each). These sparse
samples establish that instrumentation works, not latency percentiles. Validation
passed 217 x86-64 UBSan host checks, fresh BIOS/UEFI builds and the full window,
desktop, editor and graphical-login suites on both firmware paths. Partial/full
scene equivalence checks passed, and all 348 project-owned C/C++ files passed
the style checker. ASan was not used for the x86-64 host suite on this emulated
host; CI sanitizer policy remains unchanged.

The graphics optimization's sequential local QEMU comparison used seven samples
per case at 1920x1080. Median compositor wall times (including debugger overhead):

| Scene | Update | Before | After |
| --- | --- | ---: | ---: |
| Small window | One visible pixel | 137.5 ms | 26.0 ms |
| Maximized window | One visible pixel | 188.4 ms | 61.2 ms |
| Small window | Cursor only | 136.3 ms | 1.7 ms |
| Maximized window | Cursor only | 188.1 ms | 2.1 ms |
| Small window | Forced full presentation | 136.8 ms | 96.6 ms |
| Maximized window | Forced full presentation | 189.4 ms | 123.2 ms |

Raw samples and medians are under `tests/out/graphics-{reference,final}-timings/`
and `tests/out/graphics-comparison.json`. These are workload-dependent diagnostics,
not frame-rate guarantees. These historical measurements predate clipped scene
composition.

Final graphics validation passed 204 x86-64 UBSan host checks, 18 focused native
ARM Linux ASan/UBSan renderer checks, six boot-asset checks, the BIOS chain probe,
fresh BIOS/UEFI builds, and boot, windows, shell, terminal, streams, usability,
filters and isolated-user suites on both paths. Final window captures were
inspected. The stripped kernel is 248,136 bytes within the 393,216-byte BIOS
slot. Formatting and Python syntax checks passed. Source hashes and guest suite
results are in `tests/out/graphics-final-manifest.json` and
`tests/out/graphics-{bios,uefi}-validation.json`. The translated x86-64 ASan
runtime failed its empty-program preflight; full x86-64 ASan remains a native
CI requirement.

## Shell editing and directory listing

`make -C tests test-shell-editing FIRMWARE=bios` (or `FIRMWARE=uefi`)
checks blinking caret pixels and cursor position through GDB, PS/2 middle-line
insertion/deletion, wrapped lines, history, completion, cursor authorization,
batched directory ABI bounds and redirected listing contents. The window suite
uses `window --trace` for event assertions; ordinary demo launches stay quiet.

`make -C tests test-listing-performance FIRMWARE=bios` runs three samples on a
disposable image. `tests/qemu_listing_performance.py --firmware bios
--build-directory /absolute/build/directory` selects a saved build. The probe
separates legacy entry-at-a-time enumeration, tiny writes and a grouped write;
it also measures actual `ls /USR/BIN` from Enter to the next prompt. Run
benchmarks sequentially on an otherwise idle host. Guest PIT ticks and host
wall times are diagnostics, not CI thresholds or hardware guarantees.

The local BIOS comparison with 50 entries reduced median listing time from
14.84 seconds to 0.296 seconds (three samples each). Final samples were
0.256–0.340 seconds; final UEFI median was 0.308 seconds (0.299–0.363).
The change combines 16-record directory pages, one
permission-table validation per directory scan, explicit 1024-byte output
buffering and faster console scrolling. Permission sectors were already cached
by the journal; repeated metadata processing and tiny output calls both cost
time. Raw baseline and final samples are in
`tests/out/listing-baseline-bios.json` and
`tests/out/qemu-listing-performance-{bios,uefi}/timings.json`.

Validation passed 208 x86-64 UBSan host checks, 18 native ARM ASan/UBSan renderer
checks, six boot-asset checks, the BIOS chain probe, and both firmware builds.
Boot, shell, terminal, windows, storage, files, accounts, streams, usability,
filters and isolated-user suites passed on BIOS and UEFI. Final isolated builds
in `tests/out/shell-final/{bios,uefi}` additionally passed the editing/ABI
suite after making middle editing opt-in for the shared input helper. Their
kernels and the loadable segments of all 44 other compared programs match the
broad regression builds; the final shell/input sources received the focused
checks. Full translated x86-64 ASan remains unavailable as described above.

## FAT32 boot loader regressions

`python3 tests/boot_loader.py` (or `make -C tests test-boot-loader`) compiles the
production BIOS FAT32 reader and shared ELF validator with ASan/UBSan. It checks
a 600,000-byte fragmented file in a fragmented root directory, random and
cross-sector reads, EOF, disk failures, malformed geometry, partition bounds,
invalid FAT entries, truncated/cyclic chains, and malformed ELF headers/ranges.
The sector adapter reads an image file; firmware mode transitions are covered
separately by QEMU. Use a native sanitizer runtime (the native ARM Linux GCC
runtime also works for these portable C sources).

`python3 tests/qemu_loader.py --firmware bios` (and `uefi`), or
`make -C tests test-loader FIRMWARE=...`, adds a 512 KiB file-backed segment at
2 MiB plus 64 KiB BSS to the fresh kernel, relocates its FAT chain into
nonadjacent clusters, and boots it. GDB compares all loaded bytes and BSS before
entry, checks the unchanged handoff, then observes repeated timer interrupts.
Separate disposable images must emit explicit serial rejection for missing,
malformed and overlapping ELF files; BIOS also rejects corrupt/truncated FAT
chains and a volume extending past its partition. A timeout never counts as
successful rejection. No old raw-slot fallback is present.

The normal image test verifies `KERNEL.ELF` bytes in FAT32 on both paths and
requires BIOS LBAs 78–845 to remain zero. Historical size reports earlier in
this document describe the retired fixed-slot implementation.
Reports are under `tests/out/boot-loader/` and `tests/out/qemu-loader-{bios,uefi}/`.
Both suites run in CI alongside the existing boot and interactive regressions.

FAT32 loader validation passed 208 existing x86-64 UBSan checks, six boot-asset
checks, the BIOS chain probe, 15 native ARM Linux ASan/UBSan boot-parser groups,
nine BIOS and six UEFI loader cases, and fresh boot/handoff/timer checks on both
paths. Account, shell, isolated-user, window, file/editor, storage, usability and
stream suites passed on both firmware paths. Window captures were inspected.
The clean final BIOS stage 2 is 28,928 bytes within its unchanged 32 KiB slot.
Final and regression builds have matching kernel/userspace loadable segments;
the BIOS loader is byte-identical and UEFI differs only in PE header metadata.
Formatting and Python syntax checks passed. Results and hashes are recorded in
`tests/out/loader-{bios,uefi}-validation.json` and
`tests/out/loader-final-manifest.json`. Full x86-64 ASan remains a native CI
requirement; the local existing host suite used the explicit UBSan fallback.

## Window rendering foundation

The existing `tests/qemu_windows.py --firmware bios` / `uefi` suite now runs a
640×480 frame probe before interactive tests. GDB independently verifies that
staging does not affect either the committed surface or displayed pixels,
resize retains the old image, quota failure leaves geometry/pixels unchanged,
and an idle GUI is blocked in scheduler I/O state. Guest checks cover invalid
pointers/rectangles, transaction restart/cancel, stale generations, legacy-blit
rejection after opt-in, mixed window/stream readiness, timeout, keyboard wake,
close wake and released/foreign handles. The harness temporarily injects a full
surface quota, restores accounting before further allocation, and checks zero
surface pages after cleanup. Existing drag tests now cover clients above the
old 320×240 limit.

`python3 tests/surface_allocator.py` runs production bitmap/contiguous-page code
with ASan+UBSan against a synthetic ownership map (only hardware halt instructions
are replaced with traps), covering invalid requests,
exact exhaustion, fragmented free space, failed-allocation accounting and
release/reuse. It requires a native Linux C++ sanitizer runtime (set
`SURFACE_CXX` to override `g++`) and runs in the host CI job. This isolated test
does not claim coverage of the general kernel heap or hardware page mappings.

The stream inheritance probe waits for the exited child to disappear from process
records before reopening its former exclusive file. Exit-result collection can
precede idle's file cleanup; the byte and shared-position assertions remain
unchanged. Account/window cleanup checks inspect the new fixed window list and
also require all surface pages to be reclaimed.

Window foundation validation passed fresh BIOS/UEFI builds and all twelve guest
suites on each path: boot, windows, accounts, shell, isolated users, files,
storage, usability, streams, terminal, shell editing and filters. Both window
captures were inspected. The existing host suite passed 208 checks with the
explicit local x86-64 UBSan fallback, plus six boot-asset checks and the BIOS
chain probe (`BOOT_LAYOUT_CC=clang-18` selected the container's compiler).
The isolated surface allocator passed native ARM Linux ASan+UBSan. Full x86-64
ASan remains a native CI requirement. Formatting (328 files), Python syntax,
workflow lint and diff whitespace checks passed. The final firmware builds
match every modified production source; results and hashes are in
`tests/out/gui-final-validation.json` and `tests/out/gui-source-manifest.json`.

## GUI input and desktop interaction

`python3 tests/qemu_gui_input.py --firmware bios` / `uefi` (or `make -C tests
test-gui-input FIRMWARE=...`) runs the GUI probe and interaction demo on disposable
images. It verifies key transitions, repeats, modifiers/text, GUI Ctrl-C safety,
forward/reverse window cycling, minimize/maximize, focus/capture reset while keys
and buttons are held, ingress/window overflow, and ordered/coalesced close
requests with rejection and acceptance. GDB injects ingress loss and inspects
held-state recovery; it checks that recovering a lost Ctrl-C never fabricates a
GUI key/copy gesture. Close requests survive a full window event queue.

Clipboard checks include foreign/background denial, invalid buffers, 4096-byte
bounds, atomic failure, short-read rejection and zeroing across logout/login.
The visible diagnostic exercises Tab/Shift-Tab, selection/copy/cut/paste,
pointer focus, cursor/Backspace/Delete editing, exact saved file bytes, successful
save-and-close, unsaved-change cancellation/discard and save failure
while another process holds an exclusive writer. Screenshots cover fields and
the close dialog; glyph pixels verify the userspace PSF font path. Edits use
explicit key releases and wait for empty input queues plus a blocked GUI owner,
so concurrent TCG guests cannot turn host delays into overlapping key chords. Existing
window tests assert the guest pointer position before clicks and use Ctrl-Alt-Esc
for modern-window forced termination; legacy
window probes retain their Ctrl-C compatibility checks.

`python3 tests/gui_keyboard.py` runs the production keyboard decoder and existing
line-buffer cases with a native ASan+UBSan runtime, independent of the x86 host
assembly suite. It covers left/right modifiers, repeat/release, Caps Lock,
extended/fake-shift/Pause sequences and bounded arbitrary input bytes. The
ordinary host suite also includes the new GUI cases and pins syscall/record ABI
sizes. CI runs both the native decoder suite and the new guest suite alongside
the existing BIOS/UEFI regressions. Image packaging also checks the window demo
against the userspace segment/page policy, catching misplaced constructor sections.

Step-2 validation passed fresh BIOS/UEFI builds and all thirteen guest suites on
both paths: the twelve foundation suites above plus GUI input/interaction. The
final window and GUI suites use the final demo build; the other regressions use
byte-identical packaged kernels. Fields and close-dialog screenshots were
inspected on both paths. The host suite passed 210 checks with the explicit local
x86-64 UBSan fallback, six boot-asset checks and the BIOS chain probe. All 13
native keyboard/line-buffer cases passed ASan+UBSan. Formatting (331 files),
Python syntax, workflow lint and diff whitespace checks passed. Final images
and source snapshots are refreshed in `tests/out/build-bios` and
`tests/out/build-uefi`; consolidated results and production-source hashes are in
`tests/out/input-step2/final-validation.json` and
`tests/out/input-step2/source-manifest.json`.

## Userspace GUI library

`python3 tests/gui_library.py` (or `make -C tests test-gui-library`) compiles the
production `libs/gui` core and window adapter with native ASan+UBSan and strict
compiler warnings. The core checks cover extreme/clipped drawing, PSF glyphs,
flex sizing and resize, hidden/disabled subtrees, capacity rejection, partial
selection, clipboard failure atomicity, pointer cancellation, list scrolling,
modal isolation/focus restoration, paired Space-text suppression, visible scroll
indicators and 5000 bounded layout/input/render stress
iterations. Explicit syscall adapters test malformed/truncated font input,
creation cleanup, bounded frame uploads, stale upload/present cancellation,
reset snapshots, close replies and timed window waits. These tests do not
substitute drawing or widget implementations.

`python3 tests/qemu_gui_library.py --firmware bios` / `uefi` (or `make -C tests
test-gui-widgets FIRMWARE=...`) runs `window --widgets --trace` on a disposable
image. GDB independently reads the GUI tree from its owner's private physical
frames, checks committed pixels and blocked idle state, and verifies cleanup.
The suite exercises disabled-control traversal, About/close interaction, partial
selection and clipboard edits, checkboxes, list navigation, pointer selection,
modal input confinement, cancel/accept, maximize/restore and input-loss recovery.
Editing waits for empty ingress/window queues and a blocked application; pointer
clicks verify guest coordinates. Screenshots capture normal and maximized widgets
and the modal dialog. CI runs both the native and firmware suites alongside the
existing GUI input and window regressions.

Step-3 validation passed fresh BIOS/UEFI builds and all fourteen guest suites on
both paths: the thirteen step-2 suites plus the GUI library suite. The final
widget tests cover the final library/demo sources; earlier regressions use
byte-identical packaged kernels. Normal, modal and maximized screenshots were
inspected on both paths. Native GUI ASan+UBSan checks passed all 21 core groups,
5000 stress iterations and platform-adapter checks. The full host run used the
explicit local x86-64 UBSan fallback: 209 checks passed within the harness and
one storage interruption case exceeded its 15-second limit; that case passed
when run directly with the same binary and fixture. Six boot-asset checks and
the BIOS chain probe passed. Initial guest timeouts and one pointer-position
assertion passed on reduced-concurrency/isolated reruns without changing their
assertions. Formatting (337 files), Python syntax, workflow lint and diff
whitespace checks passed. Final images and source snapshots are available in
`tests/out/build-bios` and `tests/out/build-uefi`; consolidated results and
production-source hashes are in `tests/out/gui-step3/final-validation.json`
and `tests/out/gui-step3/source-manifest.json`.

## Desktop and independent terminals

`python3 tests/qemu_desktop.py --firmware bios` / `uefi` (or `make -C tests
test-desktop FIRMWARE=...`) uses normal authenticated desktop login on a fresh
disposable image. It checks independent screen grids, working directories and
input owners; pipelines/redirection and foreground Ctrl-Z/fg/Ctrl-C; taskbar
focus/minimize/restore/close and GUI launching;
maximize/restore geometry; terminal close and relaunch; whole-session logout;
recovery-console login and forced-desktop-exit recovery. GDB independently
inspects copied cell grids, endpoint/controller ownership, window state and
resource cleanup. Screenshots capture terminal windows and desktop recovery.
A guest probe verifies denied desktop control, foreign terminal access and
logout from an ordinary process, and validates terminal geometry, malformed
requests, duplicate endpoint rejection, and signal isolation after a child shell
exits. Command entry waits for each guest echo, uses explicit QMP key releases, and
checks fresh prompts/foreground ownership around job control. This prevents
host timing from outrunning bounded input queues during framebuffer-heavy TCG
scheduling. The suite fills all eight terminal
slots and verifies ninth-launch failure and cleanup. CI runs this
suite on both firmware paths.

Existing command/ABI suites explicitly select `console` login so their original
console contracts and pixel assertions remain meaningful. The desktop suite
covers the new default login path. The image validator now checks DESKTOP and
TERM ELF segment permissions, overlap and page limits alongside the GUI demo.

Step-4 validation passed fresh BIOS (15 build checks) and UEFI (13 build checks)
images and all eighteen guest suites on each path: desktop, boot, loader,
accounts, shell, userspace ABI, windows, files, storage, usability, streams,
utilities, filters, text tools, terminal settings, shell editing, GUI input and
GUI library. Final desktop screenshots were inspected on both paths. Desktop
retries exposed test timing problems; echo-paced command entry, explicit key
releases and fresh prompt/foreground checks resolved them without weakening
assertions. UEFI storage missed its journal interruption breakpoint once; the
unchanged suite passed both cases on rerun. The full host suite passed 210 checks
using the explicit local x86-64 UBSan fallback. Native ASan+UBSan GUI library,
keyboard (13), surface allocator, loader (15), and filter (11) checks passed,
as did six boot-asset checks and the BIOS chain probe. Formatting (342 files),
Python syntax, workflow lint and diff whitespace checks passed. Final images and
source snapshots are in `tests/out/build-bios` and `tests/out/build-uefi`;
`tests/out/desktop-step4/final-validation.json` records reports and image hashes,
and `source-manifest.json` verifies both builds used the final production sources.

## Graphical login

`python3 tests/qemu_login_gui.py --firmware bios` / `uefi` (or
`make -C tests test-login-gui FIRMWARE=...`) exercises graphical first-boot setup,
confirmation mismatch, masked fields, generic failed-login dialogs, successful
desktop authentication, session cleanup and return to empty fields, plus actual
shutdown and restart. GDB checks the real login widget state and zeroed secret
buffers; screenshots record masked setup/login, the error popup and session
return. No test logs contain the entered passwords. Native GUI sanitizer checks
also compare masked rendering and enforce disabled clipboard access and full
buffer clearing. Existing console suites explicitly enter text recovery before
using their original authentication/command contracts.

Graphical-login validation passed fresh BIOS/UEFI image builds and all six selected
guest suites on each path: graphical login/power, boot, accounts, shell, desktop
and GUI library. Masked-form and error-dialog screenshots were inspected. Native
GUI ASan+UBSan checks passed, including secret-field rendering, clipboard and
clearing checks. The BIOS power test was corrected to accept the expected QEMU
shutdown; recovery tests now explicitly select text mode after reboot and dismiss
the damaged-store dialog. Production code did not change during those test fixes.
Formatting, Python syntax, workflow lint and diff whitespace checks passed.
Reports/image hashes are in `tests/out/login-gui/final-validation.json`; the
adjacent `source-manifest.json` verifies both builds used the final login/library
sources.

The graphical file manager has a QEMU interaction test: `make -C tests test-files-gui
FIRMWARE=bios` (also `uefi`), after fresh image builds. It checks navigation,
text/hex previews and truncation, folder creation, file movement, deletion
confirmation/cancel, missing paths, normal-user permission errors and closing.

`make -C tests test-editor FIRMWARE=bios` (also `uefi`) exercises multiline GUI
editing, selection/clipboard, open/save/save-as, overwrite confirmation, invalid
file preservation, denied saves and negotiated close. It checks final bytes in
the image and zero surviving windows/file handles/children. Desktop tests exercise
both hardcoded icons, stacking, terminal controls and session cleanup. The login
GUI tests cover first-start administrator creation on both firmware paths.
Host GUI tests cover bounded multiline insertion/paste, selection, scrolling,
resize/clipping and invalid FAT date formatting; filesystem tests check date
preservation on save/move and directory modification on child changes.

`python3 tests/qemu_settings.py --firmware bios` (repeat with `uefi`) validates
graphical system pages, durable account/group edits, direct syscall authorization
probes, read-only guest controls and current-password-verified password changes.

`make -C tests test-formatting` checks the real userspace formatter with ASan/UBSan,
including alignment, sign/zero padding, precision, invalid formats, truncation and
field limits. `make -C tests test-formatting-qemu FIRMWARE=bios` (repeat with `uefi`)
checks the runtime and redirected console table columns in a real guest.

The desktop BIOS/UEFI test also emits 600 labelled lines to overflow the terminal
history ring, checks keyboard navigation, selects and copies text through physical
mouse/key input, checks resize dismissal and return to live output, and verifies
history/clipboard cleanup on logout. Screenshots include scrollback and selection.
Host/native graphics sanitizer tests cover ring rollover, mixed history/live
snapshots, width padding, clear wiping, repeated scroll events, reverse selection,
input reset and exact/oversize clipboard bounds.
