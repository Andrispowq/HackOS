# Development guide

[Documentation index](../index.md)

Start with [building and running](building.md) to understand the toolchain and
current build limitations. Use the [system overview](../system/index.md) to find
the component you need, and [TODO](../TODO.md) to see the next priorities.

| Guide | Purpose |
| --- | --- |
| [Building and running](building.md) | Dependencies, Make targets, debugging, and container plans |
| [Artifacts and fixtures](artifacts.md) | What belongs in Git and how generated outputs are produced |
| [Testing plan](testing.md) | The test layers and first cases to implement |

## Working conventions

The kernel is freestanding C++ with NASM assembly. BIOS stage 2 and the UEFI
loader are C. Keep host utilities separate from target code: a tool that runs
on the development machine needs a host compiler, while kernel code needs
x86-64 ELF output and the project's own runtime interfaces.

Follow the formatting of the component being changed. Avoid unrelated edits to
the vendored GNU-EFI library. Its project-specific loader lives in the
`bootloader` subdirectory; changes there should not require reformatting the
surrounding third-party sources.

Changes that cross component boundaries need coordinated updates:

- Loader handoff structures and memory-type interpretation across BIOS, EFI,
  and the kernel.
- Disk offsets across the image recipe, MBR, and BIOS readers.
- Register layouts across interrupt assembly, scheduling assembly, and C++.
- Syscall numbers, registers, and object layouts across the kernel and libc.
- Font, framebuffer, and pixel-format assumptions across loaders and drawing code.

Update the relevant system page when changing one of these contracts. Keep
future work in the TODO list instead of describing an intended interface as if
it already exists.

## Checking changes today

There is no automated test suite yet. For build changes, start from a clean
copy without generated objects and check both boot paths. For hardware-facing
changes, inspect the result in QEMU with the existing device topology. Use
copies of disk images and writable firmware state for experiments.

Keep compiler errors, linker failures, emulator startup errors, and guest
runtime failures distinct. A binary that links can still have the wrong entry
point, exceed a BIOS allocation, or fail before reaching the kernel.

The [testing plan](testing.md) describes how to turn these checks into repeatable
automation in the next iteration. No test commands or CI jobs are implemented
by that plan yet.
