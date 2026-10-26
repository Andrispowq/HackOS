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
| Keyboard and line input | Make/break bounds, Shift/Caps Lock, punctuation, extended/Pause sequences, FIFO wrap/overflow, editing and command-length limits |
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
private CR3 and boot-shell privilege flag. This verifies actual automatic CPL3
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

GDB checks that all five terminated tasks were reaped, only idle and the isolated
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
all twelve test-task stacks were reaped and only the boot shell's private
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
