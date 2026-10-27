# Programs and userspace

[System index](index.md) · [Memory](memory.md) · [Interrupts](interrupts.md)

HackOS boots into the ring-3 login agent at `~/USR/BIN/LOGIN`, with no root
credentials. `InitialiseKernel` initializes filesystems, validates the account
store, creates idle, loads LOGIN, then enables IRQs. Setup creates the initial
administrator on an empty store; successful password verification starts
`~/USR/BIN/SHELL` with the selected credentials. A missing or invalid login
program produces an explicit boot error; there is no unauthenticated shell fallback.
See [persistent accounts and recovery](accounts.md). `kernel_main` remains idle, refreshes the console and reaps dead tasks.

## Isolated programs

[proc/user](../../kernel/src/proc/user/) loads static x86-64 ELF executables into
private physical frames. Each process owns four page-table pages and up to 64
code/data/stack pages. Its 2 MiB virtual arena starts at `0x0000400000000000`;
the last 16 KiB is a zeroed, non-executable stack preceded by an unmapped guard
page. Shared kernel mappings remain supervisor-only. Multiple programs can use
the same virtual addresses without sharing their private frames.

The loader validates file and virtual ranges, requires readable segments,
rejects writable executable pages, page-sharing segments, dynamic linking,
interpreters and TLS, and caps ELF files at 1 MiB. Empty load segments are allowed.
BSS and page padding are zeroed. CR0.WP and EFER.NXE enforce permissions; CPUs
without NX cannot launch user programs. Spawn permits at most 16 queued tasks,
including idle and zombies awaiting reclamation. Each parent can also retain up
to 16 uncollected child results; it must wait for them before spawning more.

The initial interrupt frame enters CPL3 with CS `0x1b`, SS `0x23`, IF enabled,
and IOPL zero. TSS.RSP0 selects a separate kernel stack. Timer scheduling switches
CR3 and restores the 704-byte register/FPU frame. User yield uses the existing
kernel scheduling mechanism without sleeping inside a preemption guard.
Exit and ordinary user exceptions mark the task dead. Idle closes its remaining
file handles and frees private frames, page tables, name and kernel stack after
switching away. Kernel faults, NMI, double fault and machine check retain kernel
handling. Scheduling is single-CPU; AVX context is not supported.

Programs enter with RDI=`argc` and RSI=`argv`; libc preserves these when calling
`main(int argc, char** argv)`. The vector is terminated by a null pointer, and
all strings and pointers are copied into the child's own user stack above its
initial stack pointer. The stack retains SysV alignment. Spawn accepts 1–16
arguments (including `argv[0]`) and at most 1024 bytes of strings including NULs.
The original `spawn(path)` remains compatible and supplies just `argv[0]=path`.

`wait(pid, status, flags)` can collect only a direct child's result, once. With
flags zero, a live child causes the caller to enter `WAITING`, which the scheduler
skips until that child exits, faults or is killed. There is no polling loop and
no yield inside a preemption guard. `HACKOS_WAIT_NOHANG` returns zero while the
child is alive. Completion returns the PID and optionally copies an `int64_t`
status; a null status pointer discards it. Invalid flags, non-child/already-waited
PIDs and invalid/read-only output buffers return `HACKOS_ERROR` without consuming
a result. Normal exit stores the signed 32-bit program status; faults store
128 plus the exception vector, and kill stores 137.

Completion records live in the parent, separately from the child resources.
Idle can therefore reclaim a child's files, pages and stack before the parent
calls wait. A parent that exits discards its results; live children continue as
orphans and are still reclaimed by idle. No PID is reused across boots.

The old trusted ELF helpers remain internal code. The binary-only `USERTEST.ELF`
asset is retained but no longer called during boot. Neither shell launch syntax
can execute a program in ring 0. Old fixed-address libc/window binaries are
incompatible with the active ABI and must be ported and rebuilt.

## Standalone shell

[libs/shell/src/main.cpp](../../libs/shell/src/main.cpp) owns command parsing,
line editing, working-directory state, formatting and its current file handle.
Keyboard IRQs only decode and enqueue characters. The authenticated shell reads those
characters through the input syscall and yields when the queue is empty.
Console rendering and hardware operations remain kernel services.

| Command | Behavior |
| --- | --- |
| `history` | List the newest 256 submitted commands; Up/Down recalls them |
| `help`, `clear` | Show commands or clear the console |
| `print <text>` | Print literal text, including percent signs |
| `print tick`, `print time`, `print date` | Show monotonic ticks or RTC values |
| `ps [aux]`, `kill <pid>` | List tasks or stop a user program; idle, login and session shell are protected |
| `ls [-la] [path]`, `wd`, `cd <path>` | Browse the first filesystem; `~` denotes its root |
| `cat <path>`, `read <path>` | Read a complete file in bounded chunks; close its owned handle on completion |
| `<name> [args]`, `./<program> [args]`, `user <path> [args]` | Launch an isolated foreground program and wait; append a separate `&` for background execution |
| `id`, `whoami` | Show the process credentials or username |
| `users`, `groups` | List the persistent account/group registry |
| `useradd <name> [group]`, `groupadd <name>` | Root creates a locked account (default group `users`) or group and persists it |
| `usermod <user> <group>` | Root adds a supplementary group for future `runas` launches |
| `passwd [user]` | Root sets a confirmed password; defaults to its own account |
| `logout` | End this shell session and its background descendants, then return to login |
| `runas <user> <command> [args]` | Root launches a program with the selected account's credentials and waits |
| `shutdown`, `restart` | Root requests kernel ACPI power-off or platform restart |

Input supports the US layout, Shift, Caps Lock, Backspace and Up/Down arrows.
Lines longer than 255 characters are discarded. Queue overflow discards input
until Enter. There is no completion, pipelines, environment expansion, process
group/job control or blocking terminal-read API yet. Program output shares the console with the shell.

All commands are parsed once into a command name and arguments. The parser
splits on spaces/tabs (the interactive line editor accepts printable characters). Single or double quotes group
words (including empty strings); a backslash escapes the next character, also
inside quotes. Unmatched quotes, a trailing backslash or excessive arguments
reject the command before execution. Each command program validates its arguments. Unsupported counts/options print usage;
`ps aux` lists the same name/PID/state/UID/GID/parent-PID information as `ps`;
CPU/memory accounting is not available. `print` joins decoded arguments with single
spaces; quote text to preserve internal spaces. Clock shortcuts apply only to
one argument (`tick`, `time`, or `date`). Quoted/escaped paths work for `cd` and
`cat`/`read`; empty paths are rejected. The `cd` and `history` built-ins reject
background `&`. The old shell-owned `open`/`read`/`close` session is retired;
file handles remain process-owned and commands read/close their own files.

Only `cd`, `history` and `logout` execute inside the shell. Other names resolve in
`/usr/bin` (the FAT image's `USR/BIN` directory), without searching the current
directory or an environment PATH. Paths containing `/`, or beginning with `~`,
are explicit; `user <path>` remains an explicit-launch compatibility spelling.
Paths accept `/` and `~` as root, collapse repeated separators and `.`/`..`, clamp
parent traversal at root, and canonicalize FAT case. `window`, `ps aux`,
`/usr/bin/ps aux` and `./USR/BIN/PS aux` can all be launched as programs.

Processes inherit a copy of their parent's working directory. `chdir` updates
only the caller; `cd` must stay in the shell to affect later launches. The kernel
resolves relative file/program syscall paths against this directory. `wd` and
`ls` therefore work from any directory without hidden command arguments. The
shell keeps no open file handles and resolves program `argv[0]` to its path.
The program limit remains 16 arguments including `argv[0]`; the optional `user`
prefix does not consume a program argument. History retains the original line. For example:

```text
./USR/BIN/TEST alpha "two words" ""
user USR/BIN/RING3
./USR/BIN/TEST background &
```

The foreground prompt appears only after wait returns. Nonzero statuses are
reported. `spawn_foreground` copies arguments and grants input ownership before
the child can run, avoiding a spawn-to-handoff race. The shell then waits without
reading input. A bare trailing `&` uses ordinary spawn and grants no terminal
access; the shell collects background results with nonblocking waits.

Any current terminal owner can delegate foreground input to a child. Ownership
returns to that launcher automatically on exit, fault, kill or Ctrl-C. If a
launcher exits first, return links are redirected to its own launcher (ultimately
the session shell, then login), before the dead task is reclaimed. The keyboard
queue is cleared at each handoff so child input cannot accidentally execute as
shell commands. `input()` remains nonblocking: a foreground program reads events
and yields on zero. Echo and line editing are program responsibilities. Try
`./USR/BIN/INPUT` for the packaged interactive example: it echoes edited lines,
accepts `exit`, and can be interrupted with Ctrl-C.

Both Ctrl keys support Ctrl-C. With terminal focus it terminates the foreground
process with status 130 even if the program never reads input or its queue is
full. Idle records the target PID; the scheduler terminates it only after
preemption guards end. Deferred cleanup closes files/windows and frees user
memory. With window focus the target is that window's owner, including background
processes. The shell is protected; at its prompt Ctrl-C cancels the current line
and history navigation. This targets one process, not a process group; children
do not inherit terminal input.

## Command history

The shell stores the latest 256 nonblank submitted commands in a predefined
`char[256][256]` ring, with no dynamic allocation. Each entry can hold the full
255-character command plus NUL. It records original text before argument parsing,
including quotes, duplicate commands and commands that fail. Blank, cancelled
and overlong discarded lines are not added. The oldest entry is overwritten when
full; history is session-local and does not survive reboot.

Up recalls older commands, Down returns toward newer commands and finally restores
the unfinished draft present before navigation. Backspace/typing edits the recalled
copy; stored entries change only when a new command is submitted. `history` lists
the retained commands from oldest to newest (including that `history` invocation).

## Users, groups and permissions

The kernel owns a bounded persistent registry: 32 users and 32 groups, with
names of 1–31 ASCII characters matching `[a-z][a-z0-9_-]*`. Setup names the UID/GID-0 administrator. It also contains a locked
`guest` (UID/GID 1000), and groups `root` (0) and `users` (1000). New user and group IDs are allocated independently from 1001, never
reused across boots. Duplicate names, unknown groups, invalid names and full
registries fail without consuming IDs. No delete/rename operation is provided.
Account and password changes return success only after a checked write, cache
flush and read-back. New users cannot log in until root runs `passwd <user>`.
The [account store](accounts.md) defines persistence and failure recovery.

Each process owns a value copy of its UID, primary GID and up to eight distinct
supplementary GIDs. Ordinary spawn, spawn-with-arguments and foreground spawn
inherit that exact snapshot, including through grandchildren. Account membership
changes affect later logins and `spawn_as` launches; existing processes and their
ordinary children retain their credentials. There are no effective/saved IDs,
setuid executables, or user-writable credential pointers. Membership in GID 0
does **not** grant root privilege: privileged checks compare UID to zero.

The kernel requires UID 0 for power operations, creating users/groups, adding
group membership, password changes, and `spawn_as`. Root may launch any registered identity with
`runas`; this command is an administrative launcher, not authentication or
`sudo`. A non-root process cannot select any identity, including its own, through
`spawn_as`. No executable path or `argv[0]` grants privilege. An arbitrary root
program may power off, while even the canonical SHUTDOWN/RESTART executables
are denied when run as a non-root user. Root privilege is inherited by ordinary
children just like any other credential.

`kill` permits the same UID or UID 0 to stop a user process. Kernel tasks, the
login agent, authenticated shell and the calling task remain protected by this API. Wait results still
belong exclusively to the parent PID, and file/window handles remain scoped to
the owning process; sharing a UID never transfers handles or wait rights.
Terminal backspace/clear and foreground handoff depend on terminal ownership,
not root. `systemShell` identifies the protected terminal anchor only; it is no
longer a power/kill/account privilege. Ctrl-C still targets the physically
focused terminal/window process and is not a process-group signal.

For example, `groupadd staff`, `useradd alice staff`, `usermod alice users`, then
`runas alice id` displays the selected UID, primary GID and supplementary group.
`runas alice shutdown` is denied by the kernel. `runas alice input` supports
normal foreground editing and Ctrl-C; appending `&` starts without terminal
input. The shell prompt resolves its actual UID to a username. `logout` returns
to the protected login agent and ends all descendants, including background
programs launched under another UID. A manually launched nested shell can exit
to its parent; only the authenticated session shell owns session teardown.

Filesystem access remains read-only through the user ABI and has no UID/GID or
mode metadata. FAT attributes are not Unix file permissions. No filesystem
ownership or general file-write authorization is claimed. The two account files
are reserved kernel storage and cannot be opened through the user ABI.

## Syscall ABI

[include/hackos/syscalls.def](../../include/hackos/syscalls.def) defines all active
call numbers and wrapper signatures. Every active call is available from ring 3,
subject to the permissions above. Kernel code calls internal functions directly;
ring-0 `int 0x80` requests are rejected and no syscall wrappers are linked into
the kernel. Slots 1–13 are reserved for the retired raw-pointer/window ABI and
return an error rather than being reinterpreted as new operations.

| Number | Call | Result/contract |
| ---: | --- | --- |
| 0 | `exit(status)` | Does not return; libc uses it when `main` returns |
| 14 | `user_write(buffer, length)` | Literal console bytes, maximum 1024; returns count |
| 15 | `input()` | Character/event, 0 if empty, 256 for queue overflow; terminal owner only |
| 16 | `console(operation)` | 0 backspace, 1 clear; terminal owner only |
| 17 | `clock(out)` | Copies ticks and RTC fields; RTC retains existing BCD representation |
| 18 | `process_info(index, out)` | Copies one process record; 1 found, 0 end |
| 19 | `kill(pid)` | Stops another user task owned by the same UID, or any user task as root; idle/login/session shell/self protected |
| 20 | `spawn(path)` | Copies a bounded path and loads an isolated ELF; returns PID |
| 21 | `open(path)` | Returns an opaque read-only file handle |
| 22 | `read(handle, out, length)` | At most 512 bytes; returns bytes read, 0 at EOF |
| 23 | `close(handle)` | Closes a handle owned by the caller |
| 24 | `stat(path, out)` | Copies file size, attributes and name |
| 25 | `readdir(path, index, out)` | Copies one directory entry; 1 found, 0 end |
| 26 | `power(operation)` | 0 ACPI shutdown, 1 restart; UID 0 only |
| 27 | `yield()` | Gives another runnable task a scheduling opportunity |
| 28 | `spawn_args(path, argc, argv)` | Copies a bounded user pointer vector and strings; returns child PID |
| 29 | `wait(pid, status, flags)` | Collects a direct child; flags 0 block, `HACKOS_WAIT_NOHANG` polls |
| 30 | `spawn_foreground(path, argc, argv)` | Current terminal owner spawns with atomic terminal handoff and parent return |
| 31 | `window_create(spec)` | Copy a `HackosWindowSpec`; return an owned opaque handle |
| 32 | `window_blit(handle, rect, pixels)` | Copy a packed rectangle of up to 1024 pixels; return 0 |
| 33 | `window_poll(handle)` | 0 while open, 1 after the mouse closes it; nonblocking |
| 34 | `window_close(handle)` | Destroy the window and release its handle; return 0 |
| 35 | `window_state(handle, out)` | Copy geometry, focus and latest client-relative pointer state; return 0 |
| 36 | `window_event(handle, out)` | Copy one event; 1 delivered, 0 empty, error for invalid pointers/handles |
| 37 | `window_mode(handle, operation)` | Query/minimize/maximize/restore an owned window; return mode flags |
| 38 | `getcwd(out, capacity)` | Copy the caller's NUL-terminated canonical directory; 0 or error |
| 39 | `chdir(path)` | Validate a directory and update only the caller; 0 or error |
| 40 | `identity(out)` | Copy current `HackosCredentials`; 0 or error |
| 41 | `user_info(index, out)` | Copy one `HackosUserInfo`; 1 found, 0 end |
| 42 | `group_info(index, out)` | Copy one `HackosGroupInfo`; 1 found, 0 end |
| 43 | `user_create(name, gid)` | Root creates a user with a known primary GID; returns new UID |
| 44 | `group_create(name)` | Root creates a group; returns new GID |
| 45 | `user_add_group(uid, gid)` | Root adds a known supplementary group; 0, including existing membership |
| 46 | `spawn_as(path, argc, argv, uid, flags)` | Root selects a registered identity; returns PID; flag 1 requests foreground handoff and requires terminal ownership, 0 background |
| 47 | `auth_state()` | Boot login agent only: broken (0), setup (1), login (2) |
| 48 | `auth_setup(name, password)` | Boot login agent with terminal ownership only; save initial administrator to an empty store |
| 49 | `auth_login(name, password)` | Boot login agent with terminal ownership only; verify and create a shell session, returning its PID |
| 50 | `password_set(uid, password)` | Root sets a registered account password and commits the store; 0 on success |

Calls use `int 0x80`: RAX is the number, RDI/RSI/RDX/RCX/R8 hold arguments,
and saved RAX receives the result. Successful operations without another result
return zero. `HACKOS_ERROR` (`UINT64_MAX`) denotes failure;
`HACKOS_DENIED` (`UINT64_MAX-1`) denotes insufficient permission.
`HACKOS_RETRY` (`UINT64_MAX-2`) asks the login agent to yield until the retry
deadline; `HACKOS_NO_ENTROPY` (`UINT64_MAX-3`) rejects password creation when
RDRAND is unavailable or repeatedly fails. Unknown and
retired calls return `HACKOS_ERROR`. The fast `syscall` instruction remains
disabled; vector `0x81` remains kernel-only.

[abi.h](../../include/hackos/abi.h) contains fixed-width, pointer-free output
records with pinned sizes and zeroed padding. `HackosCredentials` is 48 bytes
(UID, primary GID, supplementary count, zero reserved, eight GIDs); unused GIDs
are zero. User records append a 32-byte name (80 bytes total); group records
contain GID, zero reserved and a 32-byte name (40 bytes). The process record is
now 288 bytes: its original PID/state/name prefix is followed by UID/GID and
parent PID. Old process-info consumers must be rebuilt with the new layout.
IDs accepted as syscall arguments are validated at full 64-bit width, so high
bits cannot alias root. Unknown spawn flags are rejected before task creation.
Paths are NUL-terminated within 256 bytes; normalization accepts `/`, `~` or
a path relative to the caller's working directory. Copy-in validates mapped user pages; copy-out
also requires writable pages and validates the whole range before touching it.
Read validates the destination before advancing a file offset. Input events
include `HACKOS_KEY_UP` (128), `HACKOS_KEY_DOWN` (129), and
`HACKOS_KEY_INTERRUPT` (3, protected shell/login prompt). No syscall
returns kernel addresses, virtual objects, format strings or `va_list` objects.

Each process can own eight file handles. Monotonically allocated opaque tokens
are checked against the caller's live slots; closed, stale and foreign tokens
are rejected. Exit, fault and kill close all outstanding handles. This version
has no user file-write API; windows use the owned API described below.

## Runtime and tests

The active [libc Makefile](../../libs/libc/Makefile) builds startup assembly,
user-side printing/number formatting, shared memory/string/bitmap primitives,
and generated syscall wrappers. It excludes the older allocation, file-object,
and window helpers still present in that directory. The active public headers
are `hackos/user.h`, `hackos/syscall.h` and `hackos/abi.h`. There is no user heap
API yet; shell buffers are bounded stack/static storage.

Kernel and user code compile shared primitives with their own flags. Line editing
is shared with host tests through `common/include/hackos/runtime/line_buffer.h`.
All active programs use the isolated linker layout in `libs/ring3/userspace.ld`.
The test program reads a file through the safe ABI; the ring3 demo exercises
print, timer preemption and exit. The standalone `WINDOW` program ports the old
window demo onto owned handles and pixel uploads. Launch `./USR/BIN/WINDOW`,
then drag its title bar, resize its bottom-right grip, click to focus, type to
change the colour marker, and click the client to mark a position. Close X,
press Q, or use Ctrl-C. Use the title-bar `-` / `+` / `R` controls or keys M / F / R
to minimize, toggle maximize, or restore. Click a minimized window's bottom-bar
tab to bring it back. Scrolling changes the colour marker and logs signed wheel
steps. Background windows also receive focused keyboard input;
click the desktop to type shell commands.
The old split adapters retain host coverage,
but allocation-dependent legacy libc helpers are not linked into user programs.

The [testing guide](../development/testing.md) covers shell CPL3 entry, input,
file handles, bad pointers, concurrent programs, register preservation, resource
cleanup and actual guest ACPI shutdown on both BIOS and UEFI. Further work is
listed in [TODO](../TODO.md).

## Owned windows

`HackosWindowSpec` is 48 bytes: four `uint32_t` fields (`x`, `y`, `width`,
`height`), followed by a 32-byte title buffer. Width/height specify the client
area; the kernel adds a 30-pixel title bar. Normal client widths are 96–320 pixels,
heights 30–240. Coordinates start at 1 and the complete decorated window must
fit the screen. Titles must terminate within 30 bytes and contain printable
ASCII; long labels are visually clipped before the title-bar controls.

Each process may retain four handles, with a global limit of 16. Handles grant
access only to their owner; spawning a child does not transfer them. There is
no user mapping of window storage, framebuffer addresses or kernel objects.
Closed handles are rejected, and all retained handles are released by idle when
the process exits, faults, is killed or receives Ctrl-C.

`HackosWindowRect` contains four `uint32_t` fields (`x`, `y`, `width`, `height`)
relative to the client area. Blits use tightly packed row-major 32-bit
`0xAARRGGBB` pixels (opaque copying, no alpha blending). Empty, overflowing or
out-of-bounds rectangles and uploads above 1024 pixels are rejected. The kernel
copies the entire upload into a bounded buffer before modifying the window, so
invalid source memory cannot partially paint it. Both record pointers and pixel
pointers are checked against the caller's mappings.

The mouse X button removes the window; its handle remains reserved until
`window_close` or process cleanup so the owner can observe `window_poll == 1`.
Invalid, foreign and explicitly closed handles return `HACKOS_ERROR`. Polling
does not block. Dragging, resizing, window modes, focus and input events are
described below. Raw key-up events remain future work.

Idle updates mouse closure and flushes dirty drawing under a preemption guard.
The compositor overlays windows onto the physical display after copying the
console backbuffer. Window pixels never overwrite the console backbuffer;
closing or reclaiming a window triggers redraw to restore the console beneath.

### Window interaction and events

New windows take focus. A left click raises and focuses the topmost window under
the pointer; clicking outside windows restores terminal keyboard routing. The
focused title bar is blue. Closing the focused window selects the topmost
remaining visible window, or the terminal if none remain. Ownership remains enforced
regardless of focus: another process cannot query or consume your window events.

Drag the title bar to move, or the bottom-right 8-by-8 grip to resize. Geometry
stays within the display and existing 96–320 by 30–240 client limits. Resize
clears the client buffer and queues a resize event; repaint using the current
state. Blits using obsolete geometry fail safely and can be retried after a
state query. Dragging/resizing captures the pointer until left-button release.
A press inside the client similarly captures motion/release outside the window;
client-relative coordinates are signed and may be negative or beyond its size.

`window_mode` accepts QUERY=0, MINIMIZE=1, MAXIMIZE=2 and RESTORE=3.
It returns flags MINIMIZED=1 and MAXIMIZED=2, or `HACKOS_ERROR` for an
invalid operation/handle or an unsupported maximize size/budget. Minimize hides
the window, cancels capture, and selects the next visible window for focus.
Its handle, pixels and geometry remain alive. A bottom restore bar lists hidden
windows. Restoring a minimized window makes it visible with its previous mode;
restoring a visible maximized window returns to its saved normal rectangle.
Thus a minimized maximized window takes two restores to return to normal.
Maximize is idempotent and unminimizes an already-maximized window.

Maximize uses the display except for a one-pixel border and a reserved 28-pixel
bottom bar. It disables manual dragging/resizing until restored. Displays above
4096 by 2160 or below 98 by 90 are rejected. The compositor caps aggregate
maximized client buffers plus a reservation for all 16 normal windows at 64 MiB.
Maximize/normal restore clear the client buffer and emit MOVE, RESIZE and MODE;
applications must repaint using the current geometry. Blits remain bounded to
1024 pixels, including for maximized clients. The demo tiles in both axes.

`HackosWindowState` is 32 bytes: unsigned x/y/client width/client height, signed
mouseX/mouseY, then unsigned buttons/focused. Button bits are left=1, middle=2,
right=4; focused is 0 or 1. `HackosWindowEvent` is also 32 bytes: unsigned type/key,
signed x/y, then unsigned client width/height/buttons/focused. All fields are
initialized. Types are KEY=1, MOUSE=2, FOCUS=3, MOVE=4, RESIZE=5, OVERFLOW=6,
MODE=7 and WHEEL=8. MODE stores the current mode flags in `key`. WHEEL stores
a signed 32-bit step delta in `key` (cast to `int32_t`; negative up, positive
down). Wheel input goes to the hovered client or captured client without
changing focus; hidden windows and title/restore bars receive no wheel events.
MOVE x/y are screen coordinates; other event x/y are client-relative pointer
coordinates. KEY carries the translated US-layout character or existing arrow
code (press/repeat only). Mouse motion/button snapshots go to the topmost client
under the pointer, or the captured client during a drag.

Each window has a fixed 64-event queue. Overflow discards the incomplete queue
and returns one OVERFLOW event; query `window_state` and
`window_mode(QUERY)`, then repaint if visible to resynchronize. Lost wheel steps
cannot be reconstructed from state.
Invalid output pointers do not consume an event. `window_poll` remains the close
notification, and closed windows retain their handle until explicit close or
owner cleanup. Events never expose a kernel pointer.

IRQs only decode and enqueue bounded input. A shared 128-entry ingress queue
orders keyboard and mouse input; idle applies a click's focus change before a
following key. Input loss cancels pointer capture and reports overflow. Ctrl-C
is latched separately so a full queue cannot mask it. With window focus Ctrl-C
terminates that window's owning process (and cleans all its windows), including
background owners. Without window focus it retains terminal foreground/cancel
semantics. Keyboard typeahead is cleared on focus/terminal ownership changes.

For two independent owners, launch `./USR/BIN/WINDOW &`, click the desktop,
then launch `./USR/BIN/WINDOW second &`. Switch focus by clicking their clients;
only the focused demo reacts to typed keys. Each demo paints small bounded
tiles, logs events, handles resize races, and recovers queue overflow. It processes
at most 16 events before giving repainting a turn, so pointer traffic cannot
keep a resized client black by preventing the queue from becoming empty. A
completed upload checks current dimensions and retries if geometry changed.

Composition and validated pixel uploads use clipped row copies with independent
source/destination pitches. On x86-64 these copy whole 32-bit pixels, avoiding
per-pixel function calls and byte-at-a-time video-memory copies. Window borders
paint only their edges; the client is copied once instead of first being filled
with border colour. Idle coalesces dirty updates until at least 16 milliseconds after the previous
frame, using the configured PIT frequency. A pending final frame stays dirty
until displayed, even after uploads/input stop. Boot, fault and power-off paths
can still force an immediate draw. Composition still redraws the full display;
damage tracking and a frame-presentation API are not introduced by this change.

### Command display and directory listings

Successful process start/exit diagnostics go only to the optional QEMU debug
port (0xe9). They no longer enter the graphical console or scrollback. Command
output, nonzero exit status, Ctrl-C and actual faults/errors remain visible.

`ls` prints one entry per row and suffixes directories with `/`. By default it
hides dot-prefixed names and FAT hidden entries. `-a` includes those entries
(including `.` and `..` where supplied by the filesystem). `-l` adds a type
(`d` or `-`), FAT attribute flags (`r` read-only, `h` hidden, `s` system, `a`
archive; `-` for absent flags), byte size and name. This is filesystem metadata,
not Unix ownership/permissions; the API does not expose owners or timestamps.
Use `-la`, `-al`, separate flags, or `--` to end option parsing. A single optional
path is supported; unknown flags, empty paths and extra paths report usage.
