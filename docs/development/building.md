# Building and running

[Development index](index.md) · [Boot layout](../system/boot.md) · [TODO](../TODO.md)

HackOS targets x86-64 ELF, regardless of the host CPU or operating system. The
build is being repaired: the Makefiles still select host tools in several
places, and source errors prevent a complete build. The targets below describe
the existing workflow; they are not yet a one-command setup.

## Required tools

| Tool | Purpose |
| --- | --- |
| GNU Make | Project and vendored GNU-EFI builds |
| `x86_64-elf-gcc` and `x86_64-elf-g++` | Freestanding C/C++ compilation |
| Target Binutils | ELF linking, object copying, archives, and inspection |
| NASM | BIOS, kernel, and userspace assembly |
| mtools | FAT32 formatting and image population through `mformat`, `mmd`, and `mcopy` |
| QEMU system x86 | BIOS/UEFI emulation and graphical output |
| Host C compiler | Optional utilities under `util/tools` |
| GDB with x86-64 support | Optional remote kernel debugging |

The bundled OVMF files supply UEFI firmware. Disk images are regular files;
building them does not require mounting a physical disk or loop device.

### macOS

Homebrew provides target compiler packages, so building GCC manually is not
necessary for the usual setup:

```sh
brew install x86_64-elf-gcc x86_64-elf-binutils nasm mtools qemu
x86_64-elf-gcc -dumpmachine
x86_64-elf-ld --version
```

The compiler target should be `x86_64-elf`. Apple `gcc` is a Clang entry point;
the current host invocation can emit Mach-O even with `-m64`. Selecting the
right instruction set is not enough: the object format and runtime assumptions
must also match. See Homebrew's [GCC](https://formulae.brew.sh/formula/x86_64-elf-gcc)
and [Binutils](https://formulae.brew.sh/formula/x86_64-elf-binutils) formulae.

### Linux and a reproducible toolchain

Install GNU Make, a host C/C++ toolchain, NASM, mtools, and QEMU through the host
package manager. The existing `setup` and `setup.sh` scripts only cover part of
this and do not install a target compiler. They also continue after errors.

For a portable toolchain, use a maintained package that explicitly targets
`x86_64-elf`, or automate a pinned Binutils/GCC build in an isolated prefix.
Such a script should verify source checksums, use separate source/build
directories, enable C and C++, and build the compiler and target `libgcc`
without requiring a hosted target libc. GCC documents its
[prerequisites](https://gcc.gnu.org/install/prerequisites.html) and
[cross-compiler configuration](https://gcc.gnu.org/install/configure.html).

The repository does not contain this bootstrap script yet. A
`x86_64-linux-gnu` compiler has different defaults and should not silently stand
in for the bare-metal toolchain. Clang/LLVM is another possible toolchain, but
would need its own linker, ABI, and assembly checks.

## Tool selection in Make

Project Makefiles currently assign `CC=gcc` and `LD=ld`, while some recipes call
`objcopy` and `ar` literally. Passing `CC=x86_64-elf-gcc LD=x86_64-elf-ld` changes
only part of the toolchain. `CROSS_COMPILE` is supported by GNU-EFI but is not
yet a shared project setting.

The intended common configuration is:

```make
CROSS_COMPILE ?= x86_64-elf-
CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
LD = $(CROSS_COMPILE)ld
AR = $(CROSS_COMPILE)ar
OBJCOPY = $(CROSS_COMPILE)objcopy
```

This is a proposed interface. It must be included consistently, use `CXX` for
`.cpp` files, and preserve command-line overrides. Host utilities need separate
host tool variables. GNU-EFI also needs its own PIC/ABI flags rather than the
kernel's flags being applied to every component.

Specify language standards, freestanding behavior, red-zone policy, PIE/stack
protection policy, and runtime linkage explicitly. Preserve frame pointers
where the current scheduling assembly expects them.

## Existing targets and build order

Run these targets from the repository root unless shown otherwise.

| Target | Behavior |
| --- | --- |
| `make build_bios` | Builds the BIOS stages and kernel, then constructs the image |
| `make TARGET_BIOS=false build_efi` | Builds the UEFI application and kernel, then constructs the image |
| `make buildimg` | Assembles the selected image from existing outputs; does not rebuild all its inputs |
| `make all` | Builds the selected boot path and starts QEMU |
| `make run` | Starts QEMU with an existing image |
| `make debug` / `make debug_all` | Starts QEMU and GDB, optionally building first |
| `make clean` | Removes outputs for the selected path; does not clean every userspace/GNU-EFI component |
| Bare `make` / `make init` | Cleans and builds GNU-EFI, even with BIOS selected |

BIOS is the default. Use `TARGET_BIOS=false` consistently for UEFI, including
run/debug targets. Both modes write `bin/HackOS.img`, so switching modes replaces
the previous image.

A complete build needs this dependency order:

1. Create output directories and select the full target toolchain.
2. Build `libs/libc`, then `libs/shell` and `libs/test`; `make -C util/libc copy`
   performs that sequence and copies programs into the image-input directory.
3. Build the selected loader. For UEFI, GNU-EFI's `lib` and `gnuefi` outputs
   must exist before the `bootloader` target links.
4. Build the kernel and retain both its debug ELF and the stripped BIOS copy.
5. Check payload sizes, then create/populate the selected disk image.

The top-level build currently omits step 2 and assumes some directories and
GNU-EFI outputs already exist. Generated programs are no longer tracked, so a
fresh checkout must build them explicitly. The dependency graph needs fixing
before this sequence can become a reliable top-level target.

Do not use parallel Make yet: several targets list `link` and object files as
sibling prerequisites, allowing linking to race compilation. Header and NASM
include changes also lack complete dependency tracking. Remove stale objects
when switching compilers.

The BIOS MBR and first stage can be assembled independently:

```sh
mkdir -p bin
make -C boot/BIOS/mbr mbr
make -C boot/BIOS/stage_1 bootloader
```

## Current blockers

- `AHCIDevice::GetType()` does not match the base method's `const` signature.
- BIOS stage-2 C has undeclared functions and implicit pointer/integer conversions
  rejected by newer GCC releases.
- The UEFI loader includes `<elf.h>`, which the bare toolchain does not supply.
- The BIOS QEMU recipe has a broken continuation before `-net none`.
- UEFI font paths, fallback executable naming, and `ExitBootServices` handling
  need correction; see [boot documentation](../system/boot.md).

The [TODO list](../TODO.md) includes the build-system repairs and their expected
outcomes. Do not interpret a surviving local binary as proof of a clean build.

## Running and debugging an image

Once a BIOS image exists, this direct QEMU command avoids the current broken
`run` recipe and uses a temporary disk overlay:

```sh
qemu-system-x86_64 \
  -drive file=bin/HackOS.img,format=raw \
  -snapshot -m 1G -cpu qemu64 -machine q35 \
  -smp cores=4,threads=1,sockets=1 \
  -usb -device usb-ehci,id=ehci -device qemu-xhci,id=xhci \
  -net none -rtc clock=host,base=localtime
```

For UEFI, start with an image built for UEFI and copy the variable-store template
into `bin` before attaching firmware:

```sh
cp boot/UEFI/OVMFbin/OVMF_VARS-pure-efi.fd bin/OVMF_VARS.fd
```

Add these drives to the QEMU command:

```text
-drive if=pflash,format=raw,unit=0,file=boot/UEFI/OVMFbin/OVMF_CODE-pure-efi.fd,readonly=on
-drive if=pflash,format=raw,unit=1,file=bin/OVMF_VARS.fd
```

Use a QEMU window or VNC for the framebuffer. A headless serial console requires
guest serial-output support; `-nographic` alone cannot show the graphical console.
For debugging, add `-S -s` to wait for GDB on port 1234, then load symbols from
`kernel/bin/kernel.elf` and connect with `target remote localhost:1234`.
The existing debug recipes use `-s` without `-S`, so the guest can run before
the debugger attaches. See the [QEMU options reference](https://www.qemu.org/docs/master/system/invocation.html).

Inspect ELF headers and load segments with `x86_64-elf-readelf -h -l` and compare
BIOS file sizes with the [sector allocation table](../system/boot.md).
For persistence experiments, use a disposable writable image without `-snapshot`
and keep it for post-run inspection.

The legacy `make install` writes to `/dev/sdb`. It is unrelated to emulator setup
and must not be used as a routine build or test step.

## Docker direction

The first container should package a pinned target toolchain, Make, NASM, and
mtools, then export the disk image and debug ELFs. Host QEMU can display the
result normally. The repository does not contain a Dockerfile or Compose
workflow yet.

On Apple Silicon, an arm64 Linux builder with an x86-64 cross-compiler avoids
emulating the compiler itself. An amd64 container is a possible initial
compatibility route, with extra emulation cost. See Docker's
[multi-platform build guidance](https://docs.docker.com/build/building/multi-platform/).

A later runtime image can package QEMU with TCG and VNC. Publish VNC on host
loopback, retain separate writable disk/firmware state, and treat KVM as an
optional accelerator for suitable x86 Linux hosts. The file-based mtools build
needs neither privileged containers nor host disk access. Docker packages the
tools and emulator; HackOS still runs as the guest operating system.
