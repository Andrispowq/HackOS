# Building and running

[Development index](index.md) · [Boot layout](../system/boot.md) · [Testing](testing.md)

HackOS always targets x86-64. The Docker environment supplies the compiler,
assembler, image tools, firmware, debugger, and emulator; the host needs Docker
with Compose and a web browser. Use Linux containers on Windows.

## Docker quick start

From the repository root:

```sh
docker compose up --build bios
```

Open [the console](http://localhost:6080/vnc.html?autoconnect=true&resize=scale).
It shows initialization output followed by the kernel shell. Click inside it
and try `help`, `ls`, `ps` or `print tick`. Input uses the US keyboard layout.
The shell and loaded programs run as ring-0 tasks; the window desktop and isolated
userspace remain unfinished.
The first image build compiles
GCC and Binutils from source and takes considerably longer than later cached
builds. Source changes are picked up when the service starts again.

For UEFI, stop the BIOS service first because both consoles use port 6080:

```sh
docker compose down
docker compose --profile uefi up --build uefi
```

Ctrl-C stops the foreground service. `docker compose down` removes its container
and network but retains the build volume. `docker compose down --volumes` also
removes those disposable build outputs.

To compile without starting the emulator:

```sh
docker compose build bios
docker compose run --rm bios python3 tools/container.py --firmware bios --build-only
docker compose run --rm uefi python3 tools/container.py --firmware uefi --build-only
```

The image and debug ELF live at `/output/bios/` or `/output/uefi/` in the named
`build-output` volume. With the corresponding service container present, copy
an image to the host:

```sh
mkdir -p out
docker compose cp bios:/output/bios/HackOS.img out/HackOS-bios.img
docker compose cp bios:/output/bios/kernel.elf out/kernel-bios.elf
```

`setup.sh` builds this environment; `setup` is a compatibility entry point. They
do not install packages globally or require a manually prepared cross compiler.

## What is consistent across hosts

[compose.yaml](../../compose.yaml) builds a native Linux tool container on
amd64 or arm64. GCC still emits x86-64 code on either host architecture. QEMU
uses TCG software emulation with `q35`, `qemu64`, 1 GiB RAM, four virtual CPUs,
and the same USB controller configuration. KVM, host CPU passthrough, privileged
containers, host displays, and physical disks are unnecessary. This follows
Docker's [native cross-compilation approach](https://docs.docker.com/build/building/multi-platform/).

The source checkout is mounted read-only. A fresh source copy is compiled in the
output volume on every run, preventing old objects from masking missing inputs.
The container runs as an unprivileged user. Only the browser console is published,
bound to host loopback through [Compose's port mapping](https://docs.docker.com/reference/compose-file/services/#ports).

The tool image uses a digest-pinned Ubuntu 24.04 base, GCC 15.2.0, and Binutils
2.47. Source archives have SHA-256 checks. Binutils includes the x86-64 PE/COFF
backend needed by GNU-EFI. The project supplies the small set of ELF definitions
needed by its UEFI build, so a host libc sysroot is unnecessary.

This is a consistent build and machine configuration, not a promise of identical
disk-image hashes: Ubuntu packages receive updates, and FAT timestamps, volume
IDs, build paths, and emulated RTC time can vary. Windows/macOS/Linux hosts and
both CPU architectures still need their own end-to-end checks when changing
Docker, QEMU, or the compiler versions.

## Building without Docker

Required tools are GNU Make, Python 3.9+, NASM, mtools, an `x86_64-elf` GCC C/C++
toolchain, and target Binutils. QEMU is needed to run the result; GDB with x86-64
support is needed for boot tests. The bundled OVMF files supply UEFI firmware.

On macOS, Homebrew provides `x86_64-elf-gcc`, `x86_64-elf-binutils`, `nasm`,
`mtools`, and `qemu`. Apple host tools do not emit the required ELF binaries just
because `-m64` is present. On Linux, use an explicit bare-metal toolchain rather
than relying on distribution compiler defaults. The container bootstrap recipe
is in [build_toolchain.sh](../../tests/build_toolchain.sh).

```sh
make -j2 build_bios
make run
make -j2 build_efi
make TARGET_BIOS=false run
```

`CROSS_COMPILE` defaults to `x86_64-elf-` and reaches the project and GNU-EFI
builds. `CC`, `CXX`, `LD`, `AR`, and `OBJCOPY` can also be overridden explicitly.
C++ uses GNU++17; BIOS C uses GNU11. Kernel and BIOS code disable the red zone,
PIE and stack protection; C++ code is freestanding without RTTI or exceptions.
Frame pointers remain enabled for the experimental scheduler.

| Target | Behavior |
| --- | --- |
| `make` / `make build` / `make init` | Builds the selected image; BIOS by default |
| `make build_bios` | Builds BIOS stages, kernel, libc and programs, then packages them |
| `make build_efi` | Builds GNU-EFI libraries/loader, kernel and programs, then packages them |
| `make buildimg` | Packages existing outputs for `TARGET_BIOS`; does not compile |
| `make all` | Builds the selected image and runs QEMU |
| `make run` | Runs the existing image with TCG and a graphical QEMU window |
| `make debug` | Starts QEMU paused with GDB on loopback port 1234 |
| `make clean` | Removes project build outputs and the generated GNU-EFI x86-64 tree |

Both direct Make paths write `bin/HackOS.img`. Do not build both firmware images
concurrently in the same checkout. Object/link dependencies and generated C/C++
and NASM dependency files support parallel builds within one selected path.
Clean when switching toolchains or overriding compiler flags.

For separate, always-clean output directories:

```sh
python3 tools/build.py --firmware bios
python3 tools/build.py --firmware uefi
python3 tools/run_qemu.py --firmware bios --image out/bios/HackOS.img
```

## Images and debugging

[make_image.py](../../tools/make_image.py) creates FAT32 images with mtools,
without mounts or root privileges. It checks every BIOS payload against its
fixed sector allocation before creating the image, writes complete payloads,
and only replaces the output after packaging succeeds. The boot copy of the
kernel is stripped; `kernel/bin/kernel.elf` retains debug symbols. UEFI uses
`EFI/BOOT/BOOTX64.EFI` and `UTIL/FONTS/zap-light16.psf`.

The runner uses a temporary disk overlay and a fresh copy of OVMF VARS. Changes
inside the guest disappear when it stops. Use a separate writable image for
persistence experiments. Firmware templates and retained input assets stay
unchanged.

For native debugging, run `make debug` (add `TARGET_BIOS=false` for UEFI), then:

```sh
x86_64-elf-gdb kernel/bin/kernel.elf
# In GDB:
# target remote localhost:1234
# break kernel_main
# continue
```

`gdb-multiarch` is another suitable debugger. The automated boot runner uses a
private Unix socket and a bounded timeout; see [testing](testing.md). A
framebuffer requires a graphical window or VNC: `-nographic` does not turn it
into a text console.
