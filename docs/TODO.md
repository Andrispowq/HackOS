# TODO

[Documentation index](index.md) · [Build guide](development/building.md) · [Testing plan](development/testing.md)

The first goal is a clean source build and dependable boot through both firmware
paths. Filesystem integrity and memory ownership come before enabling general
userspace or adding more devices.

## 1. Make a clean build work

- [ ] Fix `AHCIDevice::GetType()` to match `Device::GetType() const`.
- [ ] Fix BIOS stage-2 declarations and pointer/address conversions rejected by
  newer GCC releases. Keep the freestanding headers complete.
- [ ] Give the UEFI loader portable ELF declarations instead of depending on a
  host `<elf.h>` header absent from the bare toolchain.
- [ ] Centralize target `CC`, `CXX`, `LD`, `AR`, and `OBJCOPY`; keep host tools
  separate and pass the selection through all recursive builds.
- [ ] Specify component-appropriate language standards and ABI/code-generation
  flags, including red-zone, PIE, stack-protection, and frame-pointer policy.
- [ ] Make output files depend on objects/linker scripts, create directories on
  demand, track header/NASM includes, declare utility targets phony, and use
  `$(MAKE)` for recursion.
- [ ] Build libc and programs before image creation; build GNU-EFI libraries
  before its loader. A fresh checkout must not depend on ignored local binaries.
- [ ] Replace the duplicate setup scripts with one fail-fast bootstrap and a
  prerequisite check. Choose an explicit default Make target; BIOS setup should
  not require building GNU-EFI.
- [ ] Correct the BIOS QEMU continuation and remove the hard-coded physical-disk
  destination from the ordinary build workflow.

Done when a clean checkout can build each image with an explicitly selected
toolchain, rebuild changed headers, and run parallel compilation without racing
the linker.

## 2. Make both boot paths dependable

- [ ] Centralize BIOS sector offsets and reject oversized stage-2/font/kernel
  payloads before constructing the image.
- [ ] Share boot-information declarations across both loaders and the kernel;
  assert layout/field offsets and widen the memory-map count.
- [ ] Reconcile the kernel linker scripts and use the actual `kernel_main`
  entry consistently.
- [ ] Fix the UEFI font path and package the x64 fallback loader filename.
- [ ] Initialize and size EFI memory-map requests correctly; obtain a fresh map
  key at the exit boundary, check `ExitBootServices`, and retry as required.
- [ ] Check loader file, allocation, and firmware-call errors before using
  returned pointers or handing control to the kernel.
- [ ] Copy OVMF VARS into the output directory for every isolated run.
- [ ] Add a simple serial/debug reporting path for boot milestones and failures.

Done when both firmware paths reach the kernel reliably from freshly created
images and fresh firmware state, and failures identify the stage that stopped.

## 3. Protect memory and interrupt state

- [ ] Size physical-memory bookkeeping for sparse address ranges, protect
  firmware/loader data for as long as it is referenced, and bound bitmap access.
- [ ] Define page-table/frame ownership and release it correctly on process exit;
  make translation of unmapped addresses return a controlled failure.
- [ ] Honor aligned allocations after heap initialization and review DMA callers'
  assumptions about physical contiguity.
- [ ] Route page faults through the correct assembly entry/return convention.
- [ ] Fix XMM11 restoration in task switching and verify the shared register
  frame, stack alignment, and frame-pointer assumptions.
- [ ] Define standard byte-memory operations separately from pixel fills and
  reconcile the duplicated memory/string support routines.

See [memory](system/memory.md) and [interrupts](system/interrupts.md).

## 4. Make file operations trustworthy

- [ ] Honor the discovered partition offset in `FAT32Driver`; stop assuming the
  second BIOS partition and `devices[0]` are always the intended volume.
- [ ] Validate BPB fields, cluster ranges/chains, directory records, and arithmetic
  before using disk data as addresses or allocation sizes.
- [ ] Validate VFS paths, filesystem indices, failed opens, and file handles.
- [ ] Define EOF, partial I/O, and return-value behavior; propagate storage errors
  through `Device`, FAT32, and VFS instead of discarding them.
- [ ] Add explicit metadata flush/unmount behavior and verify data survives a
  restart rather than only being readable from the live cache.
- [ ] Review AHCI DMA transfers and ATA argument narrowing against the documented
  sector-count interface.

See [filesystems](system/filesystems.md) and [devices](system/devices.md).

## 5. Establish a real userspace boundary

- [ ] Validate ELF bounds, ranges, and entry points; zero the complete BSS and
  load segments into a process-owned address space with suitable permissions.
- [ ] Complete the TSS/kernel entry stack and a deliberate ring-3 entry path.
- [ ] Choose and finish the syscall entry mechanism, permit the intended user
  entry, and reconcile argument registers, including the fifth-argument mismatch.
- [ ] Replace exposed kernel objects with handles and validate/copy user buffers.
- [ ] Repair task lifecycle and context switching before enabling tasking in
  normal startup; define per-process resource cleanup and fault behavior.
- [ ] Replace the binary-only `usertest.elf` with a source-built fixture and turn
  the shell experiment into a usable program once the runtime is stable.

See [programs and userspace](system/userspace.md).

## 6. Add tests and improve the development loop

- [ ] Implement the first host tests, image checks, and bounded BIOS/UEFI QEMU
  runs described in the [testing plan](development/testing.md).
- [ ] Add a source-built FAT32 fixture and persistence tests using disposable disks.
- [ ] Add CI with a pinned toolchain and retain diagnostics as job artifacts.
- [ ] Package the builder in Docker, exporting images and debug ELFs; add an
  optional QEMU/VNC runtime once native build/boot checks are dependable.
- [ ] Replace machine-specific editor settings with portable target settings.
- [ ] Inspect/export useful contents of the legacy ext2 image, then remove or
  replace it with a documented fixture recipe.
- [ ] Either retire the unused initrd experiment or fix its bounds, error
  handling, headers, and deterministic on-disk serialization.

## Later work

- [ ] Respect framebuffer pitch/pixel format, separate byte fills from colour
  fills, and add clipping tests for drawing/window operations.
- [ ] Move keyboard command execution out of interrupt context into queued work.
- [ ] Extend USB support beyond the current UHCI experiment.
- [ ] Add secondary-processor startup, synchronization, and a scheduler designed
  for multiple CPUs before treating QEMU's core count as SMP support.
- [ ] Add networking and a useful set of user programs after process isolation
  and storage behavior are reliable.
