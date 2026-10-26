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
make -C tests build-images FIRMWARE=uefi
make -C tests test-qemu FIRMWARE=uefi
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
| Memory and strings | Byte-fill semantics, unsigned comparisons, zero length, alignment, destination guards, termination, copy return values, integer formatting |
| Bitmap and vector | Bit boundaries, full bitmap, first free bit, growth, erase, independent copies, self-assignment |
| Memory setup | Page indices and structure sizes, BIOS/EFI map interpretation, sparse/high addresses, map instance isolation, early bump allocation and alignment |
| Framebuffer | Pixels, rectangle clipping, pitch, colour fills, cursor background restoration |
| VFS and registration | Volume selection, path translation, missing files/volumes, invalid indices/handles, create/read/write/close dispatch, error propagation, slot reuse |
| FAT32 | Small, empty, long-name and cross-cluster files; seeked reads; enumeration; rewrite/create/delete persistence; partial writes, EOF, empty-file growth, zero-fill, directory extension, invalid BPBs and cyclic chains |
| ELF | Identity, malformed/truncated headers, program-header bounds, filesystem loading, segment copying and BSS zeroing |

Memory/string functions are required to follow standard C semantics. A
nonstandard `memset`, `memcmp`, `strncmp`, or `strcpy` is a failure, even if
existing callers depend on that behavior. The VFS has its own documented
status-code interface; its `fread`/`fwrite` tests check that interface.

The small C++ harness uses registered functions and an always-enabled `CHECK`
macro; it needs no third-party framework. Python runs each case in a separate
process with a 15-second timeout. A crash, failed assertion, or sanitizer report
fails that case while later cases continue. Use `--case` for debugging, not as
a substitute for a full CI run.

Kernel implementations are linked directly from `kernel/src`. The same memory
and string cases also compile the independent BIOS stage-2 C and userspace
libc C++ implementations, with their own symbols and NASM objects. Test-only symbol
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

There are two distinct emulator checks:

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

## GitHub Actions

[The workflow](../../.github/workflows/tests.yml) runs on pushes, pull requests,
and manual dispatch. Three independent jobs run host tests, asset checks, and
the BIOS-chain probe. Two further jobs build and boot BIOS and UEFI with matrix
fail-fast disabled. The firmware jobs also exercise the public parallel build
command and check the Compose configuration. A failed host test does not
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

Guest-side assertions and a serial/debug channel would enable deliberate faults,
timer register-state checks, PS/2 input, disk-write persistence, and controlled framebuffer
scenes. Ring-3 isolation, syscall pointer validation, and process teardown tests
must follow implementation of the userspace boundary.
