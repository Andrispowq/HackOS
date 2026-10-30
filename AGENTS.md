# HackOS contributor guide

## Project and current state

HackOS is a hobby x86-64 operating system with two boot paths: a custom BIOS
MBR/stage-1/stage-2 chain and a UEFI application built against vendored GNU-EFI
3.0.12. The kernel and userspace library are freestanding C++; BIOS stage 2 and
the UEFI loader are C; assembly uses NASM Intel syntax, except GNU-EFI's own
GNU assembler sources. There is no hosted C/C++ standard library in the kernel.

Paging, allocation, interrupts, PS/2 input, framebuffer graphics, ACPI/PCI
discovery, ATA/AHCI storage, FAT32, and a small VFS have implementations. This is
experimental code, not a claim that every device or boot path currently works.
The [documentation index](docs/index.md) links to the system and development
guides. Keep those pages consistent with changes to component contracts.

Tasking supports idle plus isolated ring-3 programs. The standalone login agent at
`~/USR/BIN/LOGIN` starts with a graphical sign-in/setup form (Escape selects text recovery) and authenticates before launching `DESKTOP` (or `SHELL` for `console` recovery login); idle refreshes the console and reaps
exited tasks. User programs own private frames and page tables, with shared
supervisor-only kernel mappings. Both `./` and `user` launch ring-3 programs with bounded `argc`/`argv` and
foreground waits by default; a separate trailing `&` requests background mode.
Exit results belong to the parent independently of idle resource reclamation.
All active syscalls are user interfaces; kernel code calls internal functions
directly. Terminal owners can delegate foreground input and receive it back on child exit.
Processes inherit UID/GID/group credentials; UID 0 grants power and account
administration (the boot login agent may also restart/shut down while owning the
logged-out terminal), and kill requires the same owner UID or root. Persistent accounts use two preallocated FAT32 slots; first boot creates the chosen
UID-0 administrator. Passwords use PBKDF2-HMAC-SHA256 and RDRAND salts.
`logout` terminates the session and its descendants before returning to login. Ctrl-C is deferred from
IRQ context to a safe scheduler point. Shell history is a static 256-command ring.
Step 17 adds blocking terminal reads, controller-owned process groups and
Ctrl-Z/Ctrl-C job control (`jobs`, `fg`, `bg`, `wait`, `kill %job`). Descendants
inherit groups, completion waits cover the entire job, and stopped jobs retain
resources until resumed or terminated. Tab completes commands and quoted paths.
Step 18 adds inherited stdin/stdout/stderr descriptors, shared open-file positions,
bounded pipes, ordered shell redirection and four-stage pipeline jobs. Descriptor,
child and job waits accept timeouts; poll and sleep block in the scheduler.
COM1 records loader/kernel entry, terminal output and faults with bounded hardware
waits. `echo` replaces `print`; `time` selects RTC date/time and startup ticks.
Root-only `sysinfo` reports CPU, RAM/map, block devices, PCI/USB, PS/2, display
and ACPI through copied records; see `docs/system/utilities.md`. See
`docs/system/streams.md` for the stream ABI, limits and lifecycle rules.
Normal-user `grep`, `wc`, `head`, `tail`, `tee`, `more`, `sort`, `uniq`, `cut` and `tr` support streams and
redirection. Foreground pipeline stages can read paging keys independently of
stdin. Ordinary background children do not inherit that access; terminal control
and delegation still require the individual owner. Syscall 80 queries copied terminal geometry/flags. Grep has a bounded BRE subset and
64 KiB line limit; tail retains at most 128 KiB of suffix data. Sort buffers up to
128 KiB/4096 lines; uniq/cut allow 64 KiB lines and tr streams bytes. See
`docs/system/utilities.md` for their Unix-like option subsets and bounds.
`terminal get/set/reset` controls terminal font height (even 8–64 pixels) and RGBA
colours through syscalls 81–82. Queries are public; mutations require the individual
foreground owner and clear screen/scrollback. Window settings are independent and
last until reset/close; recovery-console settings last until reset/reboot. Window
chrome keeps its native font. Ctrl+Shift+V pastes the bounded session clipboard as
ASCII text with tabs/line breaks converted to spaces. See `docs/system/utilities.md`.
Libc has a private 512 KiB heap, allocation/split helpers and userspace integer
formatting. Heap pages are NX and reclaimed on every exit path; direct heap
resizing must not be mixed with libc allocation. Process-info records are 296
bytes including PGID. See the userspace guide for bounds and non-POSIX limits.
Step-15 `xxd`, `mkdir`, `rm`, `mv` and `edit` are standalone programs. Whole-file
reads/editor saves are bounded to 32 KiB; Ctrl+S saves and Ctrl+X exits without
saving. Mutations use checked filesystem methods, reject open/read-only paths
and protect `/SYSTEM`; `mv` never overwrites and handles files only. Step 16 adds persistent UID/GID/mode metadata, owner/root `chmod`, root-only
`chown`, exclusive writable handles, checked seek/write/flush and bounded redo
journaling. `/tmp` is shared writable storage; new files/directories use
0644/0755. Recovery replays prepared transactions before mounting; corrupt
journals fail closed. See `docs/system/filesystems.md` for the format and limits.
Userspace windows support screen-relative client sizes up to 4096×2160 and a
64 MiB aggregate front/staging surface budget. Generation-checked frame
transactions preserve the last committed image during resize; allocation failure
leaves the old state intact. Window readiness shares timed `poll` with streams,
and the demo blocks between events. Legacy immediate blits remain supported
until a handle opts into frame transactions. GUI input is separately opt-in:
physical key-down/up, text/modifier/repeat events, reset snapshots and negotiated
close requests. Alt-Tab/Shift-Alt-Tab cycle windows, Alt-Esc focuses the terminal,
Alt-F4 requests close and Alt-F9/F10 control window modes. GUI Ctrl-C is an app
shortcut; Ctrl-Alt-Esc terminates the focused owner. The focused owner can access
a 4 KiB session clipboard, zeroed on logout. `window --interaction` demonstrates
Tab navigation, clipboard editing, saving and unsaved-change confirmation.
`libs/gui` provides the reusable freestanding `libgui.a`: clipped tile drawing,
PSF text, row/column layout, themed widgets, selection/scrolling, keyboard focus
and modal dialogs. `window --widgets` exercises it. No kernel ABI changes are
needed; see `docs/system/gui.md` for bounded storage and initialization rules.
Userspace windows use process-owned handles and bounded pixel uploads; idle
reclaims them on exit and composites them over the console. `USR/BIN/WINDOW`
is the standalone demo. Idle dispatches ordered keyboard/mouse ingress, focus
and pointer capture; window events use fixed queues and copied state records. See `docs/system/userspace.md` for ABI, ownership and lifecycle contracts.

Step 4 adds `libs/desktop` and `libs/term`, a session-scoped launcher/taskbar,
eight independent kernel terminal endpoints and per-terminal foreground job control.
`exit` closes one terminal; `logout` ends the whole authenticated session. Desktop
failure returns to login; `console` selects a recovery shell. See
`docs/system/desktop.md` for authority, cleanup and fixed-grid limits.

Window terminals parse a bounded 7-bit ANSI subset for 16 colours, bold/underline/
reverse, cursor motion/visibility and erasure. Styles survive scrollback/resize;
clipboard copies remain plain text. Operation 8 adds an atomic styled viewport
while legacy text records remain unchanged. Recovery-console output is unchanged.
See `docs/system/desktop.md` for supported sequences and fixed parser limits.

Step 5a adds `/USR/BIN/FILES` (`libs/files`) to the desktop launcher: paged folder
browsing, bounded read-only text/hex previews, folder creation, file rename/move
and confirmed deletion with kernel permission errors. Folder rename/delete remain
unsupported. See `docs/system/files.md`; `tests/qemu_files_gui.py` covers both firmware paths.

Step 5b adds `/USR/BIN/EDITOR` (`libs/editor`), multiline `Gui::TextDocument`,
open/save forms, selection/clipboard and unsaved-change confirmation. Files can
launch Editor and displays FAT creation/modification dates from the 296-byte
file-info record. Writes update modification dates; moves preserve file dates.
The desktop owns a bottom-layer surface with hardcoded Terminal/Files icons.
Graphical first-start account setup remains in LOGIN. See `docs/system/editor.md`.

Experimental networking supports one QEMU E1000 (`8086:100e`) with bounded
idle polling and Ethernet/ARP only. Compose/noVNC enables it by default; the direct
runner enables it with `--network`. Root-only `sysinfo network` exposes copied
link/ARP state and packet counters through the existing hardware query;
static test addresses are `10.0.2.15` and gateway `10.0.2.2`. No IP transport
or sockets exist yet. See `docs/system/networking.md` and
`tests/qemu_network.py` for packet capture, ring reuse and failure checks.

## Where to look

| Area | Starting points |
| --- | --- |
| Image creation and QEMU | Root `Makefile`, `tools/`, `compose.yaml`; `setup.sh` builds the Docker environment |
| BIOS MBR and first stage | `boot/BIOS/mbr/boot_loader.asm`, `boot/BIOS/stage_1/boot_loader.asm` |
| BIOS real-to-long-mode transition | `boot/BIOS/stage_2/src/long_mode/second_stage_entry.asm` and its included assembly |
| BIOS C loader and ELF loading | `boot/BIOS/stage_2/src/second_stage.c`, `src/elf_loader/` |
| UEFI loader | `boot/UEFI/gnu-efi/bootloader/main.c`; keep its custom code distinct from the surrounding vendored library |
| Kernel entry and initialization | `kernel/src/kernel.cpp`, `kernel/src/init/kernelInit.{h,cpp}` |
| CPU structures, exceptions, IRQs | `kernel/src/arch/x86_64/{gdt,interrupts,timer,syscall}/` |
| Paging and physical allocation | `kernel/src/arch/x86_64/paging/` |
| Heap and boot memory maps | `kernel/src/memory/` |
| ACPI and PCI enumeration | `kernel/src/acpi/`, especially `pci/pci.cpp` |
| Block devices | `kernel/src/drivers/{device,ata,ahci}/` |
| PS/2 and experimental USB | `kernel/src/drivers/{keyboard,mouse,usb}/`; PCI OHCI/EHCI/XHCI branches are placeholders |
| Desktop and terminal endpoints | `libs/desktop/`, `libs/term/`, `kernel/src/proc/user/terminals.*`; see `docs/system/desktop.md` |
| Userspace GUI library | `include/hackos/gui.h`, `libs/gui/`, `libs/window/src/widgets.cpp`; see `docs/system/gui.md` |
| Graphics, console, windows | `kernel/src/drivers/screen/`; packed RGB colour macro is `COLOUR` in `screen.h`, PSF1 font rendering is in `screen.cpp` |
| Filesystems | `kernel/src/fs/filesystem.*`, `vfs.*`, `fat32/`; low-level FAT operations are in `fat32driver.*` |
| Program loading and scheduling | `kernel/src/proc/{elf,tasking}/` |
| Accounts and login | `kernel/src/proc/user/accounts.*`, `account_store.*`, `account_disk.cpp`, `kernel/src/security/`, `libs/login/`; see `docs/system/accounts.md` |
| Standalone shell | `libs/shell/src/main.cpp`; shared line editing in `common/include/hackos/runtime/line_buffer.h` |
| Userspace runtime and programs | `libs/libc/`, `libs/shell/`, `libs/test/`; root Makefile builds them and `tools/make_image.py` packages their outputs |
| Minimal support routines | `kernel/src/lib/`; similar code also exists in BIOS stage 2 and `libs/libc/`, with independent build rules |
| Assets and old experiments | `util/fonts/zap-light16.psf`, `util/usertest/usertest.elf`, `util/tools/`; the retained ext2 image and generated initrd images are not used by the current image recipes |

Read [system documentation](docs/system/index.md) for component behavior,
[build instructions](docs/development/building.md) for toolchain constraints,
and [TODO](docs/TODO.md) for priorities. The [testing guide](docs/development/testing.md)
describes `tests/`, test containers, and the GitHub Actions workflow.

## Boot flow and contracts

BIOS: MBR relocates from `0x7c00` to `0x0600`, finds the active partition, and
loads stage 1 at `0x7c00`. Stage 1 loads stage 2 at `0x8000`. Stage 2 reads the
font to `0x6000`, requests 1920x1080x32 VBE graphics,
collects the memory map, enables long mode/SSE, and calls `loader_main`.
Stage 2 reads root `KERNEL.ELF` from FAT32 through a real-mode INT 13h bridge,
validates its segments against E820 RAM, streams them into linked addresses, and
calls `kernel_main`. A 512-byte bounce buffer at `0x20000`, E820 map at `0x30000`
and six bootstrap page-table pages at `0x40000` remain below 1 MiB.

The BIOS image uses 512-byte sectors and fixed offsets shared by
`tools/make_image.py`, the MBR partition table, and stage-1/stage-2 assembly:

| Payload | Start LBA | Reserved sectors | Maximum bytes |
| --- | ---: | ---: | ---: |
| MBR | 0 | 1 | 512 |
| Stage 1 | 1 | 1 | 512 |
| Stage 2 | 2 | 64 | 32,768 |
| PSF font | 66 | 12 | 6,144 |
| Unused compatibility gap | 78 | 768 | — |
| FAT32 data partition | 846 | 93,750 | 48,000,000 |

Both images store `KERNEL.ELF` in FAT32; the old raw slot stays empty.
The image packager in `tools/make_image.py` rejects oversized stages/font. Any
change to these limits must update all readers, writers and image assertions.
FAT32 discovery on BIOS reads the relocated MBR at `0x0600`; on UEFI it assumes
a filesystem beginning at LBA 0.

UEFI: `efi_main` opens `KERNEL.ELF`, loads ELF segments, obtains GOP graphics,
a PSF1 font, ACPI and memory-map data, calls `ExitBootServices`, then calls the
ELF entry using the SysV ABI. The packaged fallback is `EFI/BOOT/BOOTX64.EFI`;
the font is `UTIL/FONTS/zap-light16.psf`. The loader checks the final memory-map
key, retries a stale key, and rejects more than 255 descriptors.

The boot handoff and nested framebuffer/font/memory-map records are shared in
`include/hackos/boot_info.h`. C/C++ assertions pin sizes, alignment and every
field offset. Keep the 64-byte `KernelInfo` and 24-byte map-entry ABI consistent;
`rsdp_version` carries the actual ACPI revision. The BIOS/EFI memory type
numbers differ; the kernel uses `booted_from_BIOS` to select interpretation.
`tests/qemu_boot.py` independently decodes the handoff on both boot paths.

`kernel_main` initializes display/GDT/IDT/paging, then heap, devices/filesystems,
ACPI/PCI, windows, syscalls and tasking. It validates account storage and loads the unprivileged login agent before
enabling IRQs. `kernel_main` becomes idle, refreshes the console and reclaims
zombie stacks, private user pages and file handles. Keyboard decoding remains
in the kernel; command parsing and line editing run in the ring-3 shell.

## Build and validation

- Read the [build guide](docs/development/building.md) before trying to build.
  `make -C tests test` runs host checks, boot-asset checks, and a BIOS MBR/stage-1
  probe. Full BIOS/UEFI builds and GDB boot milestones are separate targets; see
  `tests/Makefile` and `.github/workflows/tests.yml`. Existing defects are reported
  as failures. A successful compiler invocation is not a successful boot.
- Host tests enforce standard C memory/string semantics and compile real sources
  with explicit hardware/allocator adapters. Do not weaken assertions to preserve
  bugs. LLVM coverage covers only the linked C/C++ subset. ASan needs a compatible
  x86-64 runtime; `--sanitizers undefined` is an explicit local fallback, not CI.
- `docker compose up --build bios` builds and runs a browser console on
  `http://localhost:6080/vnc.html`. Stop it before starting the `uefi` service.
  Containers use native amd64/arm64 cross-tools and the same x86-64 TCG guest.
  Source is read-only, build output uses a named volume, and OVMF VARS is copied.
- Bare `make` builds BIOS. `make build_bios` / `make build_efi` build the selected
  loaders, kernel and programs. `make all` also runs QEMU; use `TARGET_BIOS=false`
  for UEFI run/debug. Both direct builds replace `bin/HackOS.img` with a fresh account store; the Docker
  entry point preserves account slots across builds in its named volume.
- `CROSS_COMPILE=x86_64-elf-` selects CC/CXX/LD/AR/OBJCOPY consistently. GCC 15.2.0
  and Binutils 2.47 are bootstrapped by `tests/build_toolchain.sh`; PE support is
  required for UEFI. `boot/UEFI/include/elf.h` supplies freestanding ELF types.
- Makefile object/link dependencies and generated C/C++/NASM dependencies support
  parallel compilation. Keep frame pointers, freestanding flags and no-red-zone
  policy. Clean when changing compiler or flags; `tools/build.py` always uses a
  fresh source copy and writes separate `out/bios` / `out/uefi` artifacts.
- The packaged boot kernel strips symbols to save filesystem space; retain the separate
  debug ELF. MBR and stage 1 each must be exactly 512 bytes. Never substitute old
  binaries to hide a build failure.
- For boot changes, test BIOS and UEFI separately in QEMU using the existing
  `q35` device topology. Check ELF class, machine, entry point, and load segments
  with target `readelf`, and compare BIOS payload sizes to the table above.
  Run `tests/boot_loader.py` and `tests/qemu_loader.py --firmware bios` / `uefi`
  for sanitized parser checks, large fragmented kernels and malformed-input rejection.
  Run `tests/qemu_accounts.py`, `tests/qemu_shell.py`, `tests/qemu_user.py`,
  `tests/qemu_windows.py`, `tests/qemu_files.py`, `tests/qemu_storage.py` and
  `tests/qemu_usability.py` and `tests/qemu_streams.py` with
  `--firmware bios` and `--firmware uefi` after fresh image builds for interactive input, ABI, register-state and task-lifecycle checks.
  Use a disposable image for FAT32 write tests and a copied OVMF VARS file.
- Screen output is graphical; `-nographic` alone does not expose the framebuffer
  as a text console. Use a graphical QEMU window or VNC for visual smoke checks.
- Build and run tools operate only on image files. Do not add physical-disk writes
  to ordinary setup, build or emulator targets.

## Editing guidance

Keep changes local to the subsystem being worked on and follow its existing
formatting. Preserve freestanding assumptions, x86-64 calling conventions,
interrupt-frame layouts, linker addresses and NASM/C symbol names. Interrupt
handlers return a saved `Registers*`; the shared epilogue restores it with
FXRSTOR and IRETQ. Preserve the 704-byte frame and SysV/FXSAVE alignment. Never
yield, sleep or exit inside a `PreemptionGuard`; these single-CPU guards are not
SMP locks. Syscall numbers and argument conventions must
stay synchronized between `kernel/src/arch/x86_64/syscall/` and
`libs/libc/src/kernel_interface/`.

Do not sweep-edit the vendored GNU-EFI tree. Keep firmware and the PSF font as
inputs. `util/usertest/usertest.elf` has no source/build rule in this checkout and
must remain available until replaced reproducibly. Generated outputs are ignored
and untracked. `ext2.img` remains tracked because its contents have no regeneration
recipe. See [artifacts and fixtures](docs/development/artifacts.md) before changing
binary inputs, and do not restore generated binaries to hide missing dependencies.

New code follows the original spacing style: separate functions and logical
blocks with blank lines, one executable statement per line, and braces on loops
and all else/else-if branches. An if without else may omit braces only when its
entire statement fits on one line. Do not bulk-reformat existing code.
