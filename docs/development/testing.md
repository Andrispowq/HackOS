# Testing plan

[Development index](index.md) · [TODO](../TODO.md)

HackOS has no automated test suite or CI pipeline yet. The program in
`libs/test` and the startup call to `USERTEST.ELF` are experiments, not a harness
with assertions and a reliable pass/fail result. This page describes the next
iteration; the directories and commands proposed here do not exist yet.

## Start with three layers

| Layer | What it establishes | What it cannot establish |
| --- | --- | --- |
| Host tests | Pure algorithms, parsers, and filesystem behavior against controlled memory/disks | Privileged instructions, interrupt entry, or actual driver behavior |
| Build/image checks | Correct formats, entry symbols, sizes, layout, and packaged inputs | A working kernel or correct runtime mappings |
| QEMU tests | Boot, interrupts, device I/O, and selected kernel services in an emulated machine | General compatibility with physical hardware |

Keep quick host tests independent from boot tests so a failure can be localized
without starting a VM for every library change. Both BIOS and UEFI need their
own emulator cases even though they share a kernel.

## Host-side tests

Build the real implementation under test, not a second implementation written
for the test. Introduce small adapters for allocation, logging, clock access,
and port/MMIO operations where needed. Avoid linking the entire kernel into a
host executable.

| Area | First useful cases |
| --- | --- |
| Bitmap and containers | First/last valid element, empty/full state, allocation exhaustion, growth/erase, and out-of-range behavior |
| Memory/string routines | Zero length, alignment edges, boundaries, equality/ordering contracts, and overlap where the API promises it |
| Memory-map interpretation | BIOS versus EFI types, sparse regions, regions above 4 GiB, reserved overlaps, and allocator bounds |
| Heap | Split/coalesce, exhaustion/expansion, alignment requests, and ownership across free/reallocation |
| ELF validation | Truncated headers, overflowing offsets, invalid segment ranges, overlapping segments, and BSS zeroing |
| FAT32/VFS | Missing files, invalid handles/indices, zero-byte and cross-cluster I/O, EOF, long names, resize/delete, and malformed chains |
| Graphics | Pixel packing, clipping at every edge, nontrivial pitch, and glyph bounds in a memory-backed framebuffer |

First define the intended contracts of support routines. The project's
`memset` and `memcmp` differ from their hosted namesakes; tests must not silently
exercise the host libc or bless accidental behavior by copying it into expected
results. Separate byte-memory operations from pixel fills where appropriate.

For FAT32, implement a bounded in-memory or file-backed `Device`. Supply
deterministic RTC values and a test allocator. Generate disposable images from
small source fixtures with mtools, exercise the actual driver, flush/unmount,
then reopen and inspect through an independent reader. Include malformed BPBs,
out-of-range clusters, and cyclic chains with time/iteration limits.

Host sanitizers can help find memory errors in portable C/C++ code, but they do
not validate NASM routines or guest address-space behavior. Their build must
keep kernel symbols and allocator hooks from accidentally replacing the host
runtime's own functions.

## Build and image checks

Make these cheap failures before starting QEMU:

- Confirm ELF64/x86-64 output, expected entry points, and valid load segments.
- Check BIOS MBR/stage-1 length and `0x55aa` signatures; reject stage-2, font,
  or stripped-kernel overflow before any `dd` truncation.
- Check MBR offsets and partition extents against the image builder and readers.
- List image contents and require the kernel, font, fixture, and rebuilt programs.
- For UEFI, check executable format, fallback filename once adopted, and the
  font path used by the loader.
- Start from a clean checkout with no generated binaries; ensure image creation
  rebuilds its prerequisites and does not modify firmware templates.

Once dependency rules are repaired, add a parallel build and an incremental
header/include-change check to catch the existing race and stale-object class
of failures.

## QEMU harness

Add a small guest reporting channel before depending on unattended boot tests.
A serial console or debug port can emit named milestones and assertion results.
A dedicated emulator exit mechanism can report completion. The runner must
distinguish pass, assertion failure, timeout, and an emulator startup error;
an idle `hlt` loop or a process surviving for a few seconds is not a pass.

Proposed first milestones:

1. BIOS stage 2 or UEFI loader reaches the handoff.
2. Kernel installs memory management and its interrupt table.
3. Timer IRQs advance a counter and a deliberate fault reaches the right handler.
4. A known file is read through the storage/FAT32 path.
5. A small write is flushed, the guest exits, and the host verifies persisted data.

Use TCG as the portable baseline and fixed QEMU machine/device settings. Give
each test a temporary directory, disk copy, OVMF VARS copy, bounded timeout, and
captured output. Keep failed images and logs as test artifacts. Use snapshots
for disposable smoke tests, but a writable temporary disk for persistence tests.
No test needs a physical disk or privileged container.

The framebuffer needs a window/VNC or captured display for visual checks.
Keep those separate from serial boot assertions. Screenshots can check a
controlled drawing scene later; they should not be the only evidence that
memory, IRQs, or filesystem writes worked.

## Userspace tests come after the boundary exists

Before ring-3 tests, repair context save/restore and resource lifetime. Then
test a minimal program with arguments, file I/O, and normal exit. Add assertions
that user code cannot access kernel pages, cannot pass invalid pointers through
syscalls, and cannot corrupt another process. An invalid ELF or a faulting
program should fail that load/task rather than halt the entire system.

## Suggested layout and first iteration

Use a small layout such as `tests/host`, `tests/images`, `tests/qemu`, and
`tests/fixtures`, with one runner that reports failures consistently. Future
targets could be `make test-host`, `make check-images`, and `make test-qemu`.
They are proposed names, not current commands.

The first implementation should deliver a clean build, a handful of bitmap/
memory/ELF checks, BIOS image-limit checks, and one BIOS plus one UEFI boot
milestone. Add FAT32 persistence next. CI can run fast host/image checks on
each change and bounded emulator jobs for both firmware paths, retaining logs,
debug ELFs, and failed disk images without committing them to Git.
