# Development guide

[Documentation index](../index.md)

Start with [building and running](building.md) to understand the toolchain and
current build limitations. Use the [system overview](../system/index.md) to find
the component you need, and [TODO](../TODO.md) to see the next priorities.

| Guide | Purpose |
| --- | --- |
| [Building and running](building.md) | Dependencies, Make targets, debugging, and test containers |
| [Artifacts and fixtures](artifacts.md) | What belongs in Git and how generated outputs are produced |
| [Testing](testing.md) | Commands, coverage boundaries, reports, and GitHub Actions |

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

Run `make -C tests test` for the independent host, boot-asset, and early BIOS
checks. Run `make -C tests build-images FIRMWARE=bios` and the corresponding
`test-qemu` target for full builds and kernel milestones; repeat with `uefi`.
The [testing guide](testing.md) includes container commands and explains current
failures and the limits of host coverage.

For hardware-facing changes, also inspect the result in QEMU with the existing
device topology. Keep compiler errors, linker failures, emulator startup errors,
and guest runtime failures distinct. A binary that links can still have the wrong
entry point, exceed a BIOS allocation, or fail before reaching the kernel.
