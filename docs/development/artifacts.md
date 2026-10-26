# Artifacts and fixtures

[Development index](index.md) · [Build guide](building.md)

Generated executables, archives, images, and logs belong outside version
control. Their source, build rules, and input fixtures belong in the repository.
`.gitignore` covers the usual output locations, including extensionless programs.

## Generated outputs

| Output | Source or regeneration path |
| --- | --- |
| `bin/` | Top-level BIOS/UEFI image build and copied loader/kernel outputs |
| `kernel/bin/`, `kernel/bin-int/` | Kernel sources and kernel Makefile |
| BIOS `bin/` and `bin-int/` directories | Per-stage Makefiles and NASM/C sources |
| GNU-EFI `x86_64/` | Vendored library, application sources, and custom bootloader |
| `libs/*/bin/`, `libs/*/bin-int/` | Libc, shell, and example-program Makefiles |
| `util/libc/shell`, `util/libc/test` | `make -C util/libc copy` after toolchain/build repairs |
| Initrd utility executables | Host compilation of their adjacent `.c` files |
| `initrd.img` and the initrd utility's image | Three text fixtures passed to `init_rd_creator` |
| `.DS_Store`, `out.txt`, emulator logs | Local metadata and diagnostics; not build inputs |

The build does not yet recreate every prerequisite automatically. In particular,
userspace binaries and GNU-EFI libraries need explicit dependency ordering;
see [building](building.md). Ignored local outputs may still exist after a
repository cleanup and must not be relied on by a fresh checkout.

## Retained binary inputs

| Input | Why it remains |
| --- | --- |
| [OVMF firmware](../../boot/UEFI/OVMFbin/) | Required by the UEFI emulator path; firmware rebuilding/provenance is not part of this repository |
| [zap-light16.psf](../../util/fonts/zap-light16.psf) | Font asset used by loaders, kernel, and programs |
| [usertest.elf](../../util/usertest/usertest.elf) | Program-loading fixture with no corresponding source/build rule in the tree |
| [ext2.img](../../ext2.img) | Legacy filesystem image with no recipe for recreating its contents; not used by the current boot images |

Copy OVMF VARS into an ignored output directory before running UEFI. The tracked
file is a template, not per-run writable state. Replace the `usertest.elf` fixture
with a source-built program when its source is recovered or an equivalent test
is added. Inspect and export any useful ext2 contents before removing that image.

## The legacy initrd experiment

The initrd tools are separate from the current FAT32 boot workflow. The removed
image copies contain the same payloads as:

| Stored name | Source file |
| --- | --- |
| `test` | [test.txt](../../util/tools/initrd_creator/test.txt) |
| `test2` | [test2.txt](../../util/tools/initrd_creator/test2.txt) |
| `test3` | [test3.txt](../../util/tools/initrd_creator/test3.txt) |

To recreate the payload with the existing creator, build it as a host utility.
It currently omits standard declarations, so the command supplies those headers:

```sh
mkdir -p bin/tools
cc -include string.h -include stdlib.h \
  util/tools/initrd_creator/init_rd_creator.c -o bin/tools/init_rd_creator
bin/tools/init_rd_creator \
  util/tools/initrd_creator/test.txt test \
  util/tools/initrd_creator/test2.txt test2 \
  util/tools/initrd_creator/test3.txt test3
```

The creator writes `initrd.img` in the working directory. Its format uses native
C structure layout and writes unused header slots without initializing them,
so matching file payloads does not imply byte-for-byte reproducible images.
Before reusing it as a supported format, fix includes, bounds/error handling,
initialization, and explicit serialization. It is not needed to boot HackOS.

## Repository checks

To see tracked files that also match ignore rules:

```sh
git ls-files -ci --exclude-standard
```

The retained `ext2.img` still matches its ignore rule. Ignore rules do not stop
Git from recording changes to a file that is already tracked. Source-backed
outputs should remain untracked; do not force-add rebuilt binaries to make a
missing build dependency disappear.
