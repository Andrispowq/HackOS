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
It shows initialization output followed by a graphical sign-in screen. On first
boot, choose an administrator name and confirm an 8–128 character password, then
log in to the desktop, which opens an independent terminal shell. Use the launcher
to open more terminals or GUI widgets. **Recovery console** (or Escape) opens the
text menu; choose `console` there for a direct recovery shell. Click inside a terminal
and try `help`, `ls`, `ps` or `time --ticks`. Input uses the US keyboard layout.
Use `test alpha "two words"` to see argument passing and read a file
from ring 3, or `user USR/BIN/RING3` for
the print/preemption/exit demo. `shutdown` requests kernel ACPI power-off. The
raw-pointer libc ABI is retired. Launch `window` for the ported
window demo; drag its title, resize the bottom-right grip, click to focus,
and type/click in the client. Close X, press Q, or use Ctrl-Alt-Esc to force termination. Use
`window --interaction` for Tab navigation, clipboard fields and save/close dialogs,
or `window --widgets` for the reusable GUI library demo (buttons, checkbox,
text selection, scrolling list and modal dialogs). Program commands wait for
completion before showing the prompt; a separate trailing `&` launches in the
background. Try `input` for interactive text input; Ctrl-C returns to
the shell. Up/Down browses the fixed 256-command history, and `history` lists it.
Bare commands resolve in `/usr/bin`; directory/history/session and job controls stay in the shell.
Try `echo hello | cat > /tmp/message`, `cat < /tmp/message`, `echo again >> /tmp/message`
and `cat /missing 2> /tmp/errors`.
Normal users can run `grep -in hello /usr/hello.txt`, `wc /usr/hello.txt`,
`head -n 5 /usr/hello.txt`, `tail -c 32 /usr/hello.txt`, and
`cat /usr/hello.txt | tee /tmp/copy | more`. Pipelines support up to four stages, with
ordered redirections and quoted paths. `cat` preserves exact bytes.
`sort`, `uniq`, `cut` and `tr` also run as normal users; try
`cut -d: -f1 /tmp/records | tr A-Z a-z | sort | uniq -c`.
Each supports `--help`; see the [utility limits](../system/utilities.md).
Use `terminal get` to inspect console settings, `terminal set font_size 32`
for larger text, and `terminal reset` to restore the default size and colours.
Settings clear the visible console and require foreground ownership.
Tab completes commands and paths. Ctrl+Z stops a foreground job; `jobs`, `fg`,
`bg`, `wait` and `kill %job` manage its process group.
The command suite is built from `libs/commands` and packaged as separate ELF
programs. Step 15 adds `xxd`, `mkdir`, `rm`, `mv` and `edit`. Try `mkdir /notes`
and `edit /notes/hello.txt`; Ctrl+S saves and Ctrl+X exits without saving. The
editor accepts ASCII/LF/tab text up to 32 KiB. `mv` refuses existing destinations.
Step 16 adds `chmod 600 /notes/hello.txt` and `chown alice staff /notes/hello.txt`.
Ordinary users create files under `/tmp` or directories root grants them; `/` and
`/usr` are owned by root. `ls -l` reports UID/GID and permission bits.
Guest file changes persist across normal runs, but rebuilds preserve only accounts. The initial administrator has UID 0 regardless of its chosen name. Try `id`,
`users`, `groups`, `groupadd staff`, `useradd alice staff`, `passwd alice`, and
`runas alice id`. `logout` ends the whole session and returns to login; `exit` closes only the current terminal. New users
and the built-in `guest` stay locked until root sets their password. Account
changes persist across stops/restarts and Docker rebuilds. `runas alice shutdown`
is denied by the kernel. See [accounts and recovery](../system/accounts.md). Use `cat /usr/hello.txt` (or `read /usr/hello.txt`) instead of the old
shell-owned open/read/close session. Explicit paths still work.
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
removes the images **and saved accounts**. BIOS and UEFI keep separate stores.

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
uses TCG software emulation with `q35`, `qemu64,+rdrand`, 1 GiB RAM, four virtual CPUs,
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
Frame pointers remain enabled for the experimental scheduler. The userspace
linker includes large-model `.lrodata`, `.ldata` and `.lbss` sections in their
respective page-aligned segments; leaving them as orphans can share a read-only
page with writable data and cause the kernel to reject the program. The image
validation checks every command ELF for segment page overlap and page limits.

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

### Shared runtime sources

`common/runtime.mk` compiles shared sources into each consumer's own
`bin-int/common/` directory with that component's freestanding flags. Kernel and
libc link independent copies; there is no shared runtime binary. BIOS stage 2
uses only the C splitter from this library and retains its other C primitives.
The old kernel/libc header paths forward to shared headers so existing source
includes remain usable. Consumers also need `common/include` and `include` on
their include path; the program Makefiles and guest-test builder supply both.
Generated dependencies track the common headers and syscall catalogue. The
active libc includes a private user heap, allocation-based split and checked
userspace formatting; old raw-pointer kernel helpers remain excluded; only userspace links syscall wrappers. The kernel calls services directly.

Libc is recreated when relinking so archive members removed by an extraction
cannot remain as stale definitions.

For separate, always-clean output directories:

```sh
python3 tools/build.py --firmware bios
python3 tools/build.py --firmware uefi
python3 tools/run_qemu.py --firmware bios --image out/bios/HackOS.img
```

## Images and debugging

[make_image.py](../../tools/make_image.py) creates FAT32 images with mtools,
without mounts or root privileges. It checks the BIOS stages and raw font against their
fixed sector allocations before creating the image, writes complete payloads,
and only replaces the output after packaging succeeds. The boot copy of the
kernel is stripped; `kernel/bin/kernel.elf` retains debug symbols. UEFI uses
`EFI/BOOT/BOOTX64.EFI` and `UTIL/FONTS/zap-light16.psf`.

The runner writes to the selected image so accounts survive guest shutdown.
Pass `--snapshot` to `tools/run_qemu.py` for a disposable run. OVMF VARS still
uses a fresh copy, and firmware templates stay unchanged.

Direct `make build*`/`buildimg` and `tools/build.py` create fresh account stores.
Back up a used image before replacing it, or use `tools/build.py --preserve-accounts`
with the same output directory. The Docker entry point enables preservation by
default: it copies both opaque 8192-byte slots into the freshly built image,
verifies the copy, and replaces the old image only after success. Corrupt slot
contents are preserved for kernel recovery; missing or wrong-size files abort
the build instead of silently resetting accounts. An image from before step 14
must be moved aside explicitly before the first Docker rebuild. No other guest
filesystem changes, including ownership/mode changes, are preserved by rebuilding.
Step-16 images require the reserved FAT storage extension; older volumes cannot
be mounted directly. A preserving rebuild can migrate their accounts into the
newly packaged filesystem.

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

The kernel compiles the account registry and syscall dispatcher, plus `window.cpp`, `window_manager.cpp`, `proc/user/windows.cpp`
and `mouse.cpp` with `-Os`; password hashing uses `-O2` for its bounded work factor. Framebuffer and console
pixel loops also use `-O2`; freestanding and interrupt ABI flags remain intact.
Both firmware paths load `KERNEL.ELF` from FAT32; there is no fixed kernel disk
slot. Stage 2 still has a 32 KiB reservation. Debug information,
frame pointers, freestanding flags and the no-red-zone policy are retained.

## Source formatting

Use clang-format **18.1.8** with the root `.clang-format`. Run
`python3 tools/format.py` to check, or add `--write` to apply it. Set
`CLANG_FORMAT` if the executable is not on PATH. The tool selects project-owned
C/C++ in kernel/common/libs/include/tests/util and BIOS, plus the custom UEFI
loader only. GNU-EFI and supplied UEFI headers are excluded. It preserves file
modes. New code uses Allman braces, four spaces, separate statements and blank
lines between definitions; keep logical blocks separated where helpful.

COM1 logging can be captured independently of the graphical console:
`python3 tools/run_qemu.py --firmware bios --image out/bios/HackOS.img --serial-log out/bios/serial.log`.
Use `--firmware uefi` with its own image for UEFI. The parent log directory must
exist. See [streams and logging](../system/streams.md) for the bounded UART and
password-echo contracts.

`libs/gui` builds `libgui.a`; the window demo links it before `libc.a`. Consumers
include `hackos/gui.h`. The window Makefile checks the GUI archive dependencies
on every invocation; the root clean target also removes GUI library outputs.
See [the GUI programming guide](../system/gui.md).

The root userspace target also builds `libs/desktop` and `libs/term`, both linked
with `libgui.a`. Images package them as `/usr/bin/desktop` and `/usr/bin/term`.
Desktop authority is granted by authenticated login, so starting another copy
from the shell does not grant session window-management rights.

`libs/files` builds the graphical file manager, packaged as `/usr/bin/files` and
listed as **Files** in the desktop launcher. See [file manager](../system/files.md).

`libs/editor` packages `/usr/bin/editor`, available from the launcher and Files.
Image packaging uses UTC for FAT timestamps to match the default QEMU RTC.

For experimental Ethernet/ARP bring-up, add `--network` to `tools/run_qemu.py`.
It enables one E1000 with QEMU user networking; the direct runner defaults to no NIC.
Both Docker Compose/noVNC services now pass `--network` automatically. Log in as
an administrator and run `sysinfo network` to see the gateway exchange. See
[networking](../system/networking.md) for static test addresses and diagnostics.
