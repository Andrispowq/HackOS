# Programs and userspace

[System index](index.md) · [Memory](memory.md) · [Interrupts](interrupts.md) · [Streams and waits](streams.md)

HackOS boots into the ring-3 login agent at `~/USR/BIN/LOGIN`, with no root
credentials. `InitialiseKernel` initializes filesystems, validates the account
store, creates idle, loads LOGIN, then enables IRQs. Setup creates the initial
administrator on an empty store; successful password verification starts
`~/USR/BIN/DESKTOP` with the selected credentials, or `~/USR/BIN/SHELL` for
`console` recovery login. The desktop starts independent terminal shells. A missing or invalid login
program produces an explicit boot error; there is no unauthenticated shell fallback.
See [persistent accounts and recovery](accounts.md). `kernel_main` remains idle, refreshes the console and reaps dead tasks.

## Isolated programs

[proc/user](../../kernel/src/proc/user/) loads static x86-64 ELF executables into
private physical frames. Each process owns four page-table pages and up to 64
code/data/stack pages, plus up to 128 private heap pages. Its 2 MiB virtual arena
starts at `0x0000400000000000`;
the last 16 KiB is a zeroed, non-executable stack preceded by an unmapped guard
page. ELF segments stay below the heap at `0x0000400000100000`. Shared kernel
mappings remain supervisor-only. Multiple programs can use
the same virtual addresses without sharing their private frames.

The loader validates file and virtual ranges, requires readable segments,
rejects writable executable pages, page-sharing segments, dynamic linking,
interpreters and TLS, and caps ELF files at 1 MiB. Empty load segments are allowed.
BSS and page padding are zeroed. CR0.WP and EFER.NXE enforce permissions; CPUs
without NX cannot launch user programs. The shared loader permits at most 64 queued tasks,
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
characters through a blocking terminal-read syscall; an empty queue puts it in
`INPUT` state until input or an ownership change wakes it.
Console rendering and hardware operations remain kernel services.

| Command | Behavior |
| --- | --- |
| `jobs`, `fg [job]`, `bg [job]`, `wait [job]`, `kill %job` | List jobs, resume in foreground/background, wait, or terminate an owned group; job numbers are PIDs |
| `history` | List the newest 256 submitted commands; Up/Down recalls them |
| `help`, `clear` | Show commands or clear the console |
| `echo [-n] [text...]` | Join literal arguments with spaces; `-n` suppresses the final newline |
| `time [-d|--date] [-t|--time] [-k|--ticks] [-a|--all]` | Show the RTC date/time and ticks since kernel timer startup; flags combine |
| `sysinfo [section]` | Root-only hardware report; default `full`; see [utilities](utilities.md) |
| `ps [aux]`, `kill <pid>` | List tasks or stop a user program; idle, login and session shell are protected |
| `ls [-la] [path]`, `wd`, `cd <path>` | Browse the first filesystem; `~` denotes its root |
| `cat [path]`, `read [path]` | Copy a file or stdin to stdout in bounded chunks, preserving exact bytes |
| `terminal` | Normal-user appearance queries and foreground-only settings; see [terminal appearance](utilities.md#terminal-appearance) |
| `grep`, `wc`, `head`, `tail`, `tee`, `more` | Normal-user stream filters and pager; see [options and limits](utilities.md#text-filters-and-pager) |
| `xxd <file>` | Load at most 32 KiB and print offsets, hex, binary and ASCII columns |
| `mkdir <path>` | Create one directory with an existing parent |
| `rm <file>` | Remove one regular file; no directories or recursion |
| `mv <source> <destination>` | Move/rename one regular file; destination must be a new full file path on the same volume |
| `edit <file>` | Edit at most 32 KiB of ASCII/LF/tab text; Ctrl+S saves, Ctrl+X exits without saving |
| `<name> [args]`, `./<program> [args]`, `user <path> [args]` | Launch an isolated foreground program and wait; append a separate `&` for background execution |
| `id`, `whoami` | Show the process credentials or username |
| `users`, `groups` | List the persistent account/group registry |
| `useradd <name> [group]`, `groupadd <name>` | Root creates a locked account (default group `users`) or group and persists it |
| `usermod <user> <group>` | Root adds a supplementary group for future `runas` launches |
| `passwd [user]` | Root sets a confirmed password; defaults to its own account |
| `logout` | End the entire authenticated session and its descendants, then return to login |
| `exit` | Exit this shell and its jobs; a window terminal closes without ending sibling terminals |
| `runas <user> <command> [args]` | Root launches a program with the selected account's credentials and waits |
| `shutdown`, `restart` | Root requests kernel ACPI power-off or platform restart |

Input supports the US layout, Shift, Caps Lock, Backspace and Up/Down arrows.
Lines longer than 255 characters are discarded. Queue overflow discards input
until Enter. Tab completes built-ins, `/usr/bin` commands and file/directory paths.
Completion matches FAT case, extends a common prefix and lists ambiguous matches;
unique directories gain `/`, files/commands gain a space. Quotes and escaped spaces
are decoded and safely re-escaped; `cd` offers directories only. Scans are bounded
to 1024 directory entries and the 255-character line limit. Pipeline/redirection
boundaries are recognized. Environment expansion remains unsupported.

All commands are parsed once into a command name and arguments. The parser
splits on spaces/tabs (the interactive line editor accepts printable characters). Single or double quotes group
words (including empty strings); a backslash escapes the next character, also
inside quotes. Unmatched quotes, a trailing backslash or excessive arguments
reject the command before execution. Each command program validates its arguments. Unsupported counts/options print usage;
`ps aux` lists the same name/PID/state/UID/GID/parent-PID/process-group information as `ps`;
CPU/memory accounting is not available. `echo` joins decoded arguments with single
spaces; quote text to preserve internal spaces. Leading `-n` options suppress the
newline; all other arguments, including `tick`, `date`, `time` and `--`, are literal.
The separate `time` utility reads the clock. Quoted/escaped paths work for `cd` and
`cat`/`read`; empty paths are rejected. The `cd` and `history` built-ins reject
background `&`. The old shell-owned `open`/`read`/`close` session is retired;
file handles remain process-owned and commands read/close their own files.

Directory/history/session operations and job-control commands execute inside the shell. Other names resolve in
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

The foreground prompt appears after the whole job finishes or stops. Nonzero statuses are
reported. `spawn_foreground` copies arguments and grants input ownership before
the child can run, avoiding a spawn-to-handoff race. `spawn_job` additionally
creates a distinct process group and records the caller as its controller. The
shell uses this for simple launches and `spawn_io` for redirected/pipeline jobs,
then waits without reading input. A bare trailing
`&` grants no terminal access; completed background jobs are collected and
reported at command boundaries, without interrupting a partially typed line.

Any current terminal owner can delegate foreground input to a child. Ownership
returns to that launcher automatically on exit, fault, kill or Ctrl-C. If a
launcher exits first, return links are redirected to its own launcher (ultimately
the session shell, then login), before the dead task is reclaimed. The keyboard
queue is cleared at handoff so child input cannot accidentally execute as shell
commands. Foreground pipeline stages created by `spawn_io` share terminal input,
including a pager whose stdin is a pipe; handoff between those peers preserves
queued keys. Ordinary descendants do not inherit this access. Explicit foreground
delegation retains exclusive input for that child. Console control/delegation
still require the individual terminal owner. `input()` remains a compatible nonblocking poll.
`terminal_read(0)` atomically checks the queue and sleeps; flag
`HACKOS_INPUT_NONBLOCK` polls instead. Both return decoded key/control events or
overflow, and deny a background reader immediately. Enqueue, overflow and terminal
handoff wake blocked readers; they recheck foreground input access before consuming anything.
Shell, login/password entry, input demo and editor use blocking reads. Echo and
line editing are program responsibilities. Try
`./USR/BIN/INPUT` for the packaged interactive example: it echoes edited lines,
accepts `exit`, and can be interrupted with Ctrl-C.

Both Ctrl keys support Ctrl-C. With terminal focus it terminates the foreground
process group with status 130 even if its members never read input or its queue
is full. Idle records the target group; the scheduler terminates it only after
preemption guards end. Deferred cleanup closes files/windows and frees user
memory. With legacy window focus the target is that window's owner, including background
processes. Rich-input windows receive Ctrl-C as a chord; Ctrl-Alt-Escape terminates their owner. The shell is protected; at its prompt Ctrl-C cancels the current line
and history navigation. Ordinary children inherit the group, including `runas`
descendants. A live owner delegates console ownership explicitly. If it exits with surviving
group members, ownership returns to a surviving launcher or another member of
that foreground group. The shell continues waiting until the whole group ends;
terminal return links survive leader reclamation.

## Jobs and stopped processes

Ctrl-Z with terminal focus stops all members of the foreground group and returns
input to its controller. `STOPPED` processes are skipped by the scheduler and
retain their address spaces, files and windows. `jobs` lists running/stopped
jobs. `fg [job]` resumes a group, restores its last reader and waits; `bg [job]`
resumes without input. Omitting the job selects the newest tracked job. Numeric
IDs and `%ID` both work. `kill %ID` terminates the complete owned group;
`kill PID` retains the existing per-process permission policy. `wait [job]`
waits without changing input ownership; bare `wait` visits every tracked job.
A stopped result is reported without consuming its completion record. Ctrl-C
cancels a session-shell `wait` while leaving its background job running.

`job_wait` validates the controller record, flags and optional writable status
pointer before blocking. It returns zero for a running job with `NOHANG`,
`HACKOS_JOB_STOPPED` with status 148 for a stopped group, or the leader PID and
its exit status after all group members exit. `HACKOS_JOB_INTERRUPTED` cancels a
session-shell wait without changing its status buffer or consuming the result.
Only the completed result is consumed. Invalid/repeated waits do not consume
another result. `job_control`
queries running (1), stopped (2) or completed (0), or atomically resumes/transfers
input/terminates an owned job. Mutations require the controller to own terminal
input. No caller can attach a process to another controller's group. A nested
shell can create its own jobs; controller exit also ends those jobs. Session
logout still ends every descendant, including stopped jobs under other UIDs.

This is bounded single-terminal job control, not POSIX signals: there are no
signal handlers, `setpgid`/`setsid`, or automatic SIGTTIN/SIGTTOU.
A resumed background reader receives `HACKOS_DENIED`. Stopped windows retain
existing pixels; raw PS/2 scancodes and modifier/key-up records are not exposed
by the decoded terminal API.

## Standard streams and pipelines

Processes inherit local stdin/stdout/stderr descriptors; `cat` reads stdin when
no path is supplied. The shell supports `|`, `<`, `>`, `>>`, `2>` and ordered
`2>&1`, including quoted paths and background/stopped pipelines. Four stages
share one controller-owned job; its last stage supplies completion status.
See [streams, deadlines and serial diagnostics](streams.md) for descriptor
ownership, pipe lifecycle, poll, timed waits, bounds and syscall slots 66–78 and 80.

## Private heap and runtime helpers

`heap_resize(bytes)` commits a page-rounded prefix of the fixed 512 KiB heap at
`HACKOS_HEAP_BASE`. It returns that base or `HACKOS_ERROR`; zero releases it.
New pages are zeroed, writable, user-only and non-executable. A failed growth
rolls back new frames without disturbing existing bytes. Shrinking unmaps whole
pages and flushes translations before return. The process owns the frames, and
idle reclaims all remaining heap pages after exit, fault or kill. The fixed
image-page quota and stack guard remain independent of the heap quota.

The active libc supplies 16-byte-aligned `malloc`, checked `calloc`, `realloc`
and `free`, with block splitting/coalescing and growth on demand. `malloc(0)`
returns null; allocation failure leaves existing data intact; `realloc(p, 0)`
frees it. Freed blocks are reused inside the process; libc retains committed
pages until process exit. Do not mix direct heap resizing with libc allocation.
`new`/`new[]` and sized/unsized delete use this heap; allocation failure through
`new` exits with status 127 because exceptions are disabled. The allocation-based
`split` helper is linked again; callers free each string and the pointer vector.

`printf`, `snprintf` and `vsnprintf` format in userspace with literal checked
writes, supporting `%s`, `%c`, signed/unsigned decimal, hex, pointers, `%%` and
`l`/`ll`/`z` lengths. Numeric widths, `-` left alignment, `0` numeric padding,
string precision and integer precision are supported (each width/precision is
bounded to 0–1024). For example, `%-16s %8u %08x` produces aligned columns.
Precision overrides zero padding, and signs/pointer prefixes precede zero padding.
Floating point, dynamic `*` widths/precision, and unsupported formats return -1.
Bounded formatting NUL-terminates when capacity is nonzero and returns the
untruncated character count. Physical-address allocation and legacy raw kernel
objects remain unavailable; window programs use the existing owned-handle ABI.

## Shell editing and caret

The shell supports Left/Right, insertion at the caret, Backspace and Delete.
Up/Down recalls history; Tab completes the prefix before the caret and preserves
the trailing text. The 255-character command bound and overflow rejection remain
unchanged. The visible two-pixel caret blinks every 500 ms, follows wrapped lines,
and uses the terminal foreground colour. It is a graphical overlay, so it never
appears in redirected output or serial logs. The shell hides it while executing
commands; the kernel also suppresses it whenever its requesting process no longer
owns the terminal. Other users of `LineBuffer` retain end-only editing unless
they explicitly enable middle editing.

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

The kernel requires UID 0 for power operations (except the boot login agent
while it owns the logged-out terminal), creating users/groups, adding
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
the owning process; sharing a UID never transfers handles or wait rights. Stream
descriptors are explicitly inherited or mapped by spawn; see [streams](streams.md).
Terminal backspace/clear and foreground handoff depend on terminal ownership,
not root. `systemShell` identifies the protected terminal anchor only; it is no
longer a power/kill/account privilege. Terminal Ctrl-C targets the foreground group; legacy window Ctrl-C retains its
window-owner target; rich-input windows use Ctrl-Alt-Escape for forced termination. Job control is authorized by the controller/child record,
not by a matching UID or an executable name.

For example, `groupadd staff`, `useradd alice staff`, `usermod alice users`, then
`runas alice id` displays the selected UID, primary GID and supplementary group.
`runas alice shutdown` is denied by the kernel. `runas alice input` supports
normal foreground editing and Ctrl-C; appending `&` starts without terminal
input. The shell prompt resolves its actual UID to a username. `logout` returns
to the protected login agent and ends all descendants, including background
programs launched under another UID. A manually launched nested shell can exit
to its parent with `exit`. Authenticated terminal shells and the desktop can
request whole-session teardown; ordinary children cannot.

Filesystem ownership and mode bits are stored in the versioned FAT extension.
Ancestor search, file read/write, executable loading and directory mutations
are checked in the kernel, including `runas` targets. `chmod` requires owner or
root; `chown` requires root. New files/directories inherit UID/primary GID and
modes 0644/0755. `/tmp` is the shared writable directory. The two account files
remain kernel-only and `/SYSTEM` mutations are forbidden. See the complete
[permission and recovery contract](filesystems.md).

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
| 15 | `input()` | Character/event, 0 if empty, 256 for queue overflow; owner or foreground pipeline peer only |
| 16 | `console(operation)` | 0 backspace, 1 clear, 2 query ownership without consuming input; terminal owner only |
| 17 | `clock(out)` | Copies ticks and binary calendar fields (full year, month 1–12); 0 or error |
| 18 | `process_info(index, out)` | Copies one process record; 1 found, 0 end |
| 19 | `kill(pid)` | Stops another user task owned by the same UID, or any user task as root; idle/login/session shell/self protected |
| 20 | `spawn(path)` | Copies a bounded path and loads an isolated ELF; returns PID |
| 21 | `open(path)` | Returns an opaque read-only file handle |
| 22 | `read(handle, out, length)` | At most 512 bytes; returns bytes read, 0 at EOF |
| 23 | `close(handle)` | Closes a handle owned by the caller |
| 24 | `stat(path, out)` | Copies file size, attributes, name, UID/GID/mode; `HACKOS_NOT_FOUND` for a missing component |
| 25 | `readdir(path, index, out)` | Copies one directory entry; 1 found, 0 end |
| 26 | `power(operation)` | 0 ACPI shutdown, 1 restart; UID 0 or boot login agent owning the logged-out terminal |
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
| 51 | `file_save(path, data, size, flags)` | Replace an existing file, or exclusively create with flag 1; at most 32768 bytes; checked flush before success |
| 52 | `mkdir(path)` | Create one directory; existing destinations/missing parents fail |
| 53 | `unlink(path)` | Remove a regular file only |
| 54 | `rename(source, destination)` | Move a regular file to a new full path on the first filesystem; never overwrite |
| 55 | `open_mode(path, flags)` | Read=1, write=2, create=4; at least read/write required; create requires write; existing files are not truncated |
| 56 | `file_write(handle, data, length)` | Owned writable handle; maximum 512 bytes and 32 KiB resulting offset; commits before returning byte count |
| 57 | `seek(handle, offset)` | Absolute offset up to 32768; returns offset; gaps zero-filled on write |
| 58 | `flush(handle)` | Flush filesystem for a valid owned handle; zero or error |
| 59 | `chmod(path, mode)` | Owner/root only, nine permission bits (0000–0777); zero or error |
| 60 | `chown(path, uid, gid)` | Root only, registered user/group IDs; zero or error |
| 61 | `terminal_read(flags)` | Blocking decoded input; flag 1 polls; owner or foreground pipeline peer only |
| 62 | `job_wait(group, status, flags)` | Owned group completion/stop; flag 1 polls; validates optional status before blocking |
| 63 | `job_control(group, operation)` | Owned job: 0 foreground, 1 background, 2 terminate, 3 query |
| 64 | `heap_resize(bytes)` | Commit/release a private page-rounded heap prefix up to 512 KiB; return heap base |
| 65 | `spawn_job(path, argc, argv, flags)` | Create a controlled process group; flag 1 requests atomic foreground handoff |
| 66–78 | Stream descriptors, pipe, poll, timed waits, sleep and mapped spawn | See the [complete stream ABI](streams.md#descriptor-abi) |
| 79 | `hardware(section, index, out)` | UID 0 only; copy a zero-initialized 128-byte `HackosHardwareInfo`; 1 record, 0 end, error otherwise |
| 80 | `terminal_info(fd, out)` | Copy 16-byte geometry and foreground flags; no root requirement; see [streams](streams.md#descriptor-abi) |
| 81 | `terminal_get(out)` | Copy a 24-byte `HackosTerminalSettings` snapshot; no ownership/root requirement |
| 82 | `terminal_set(key, value)` | Foreground owner only; set console appearance or reset defaults |
| 83 | `terminal_cursor(back, visible)` | Owner only; back=0–255 cells from current output position, visible=0/1; reset blink phase; zero or error |
| 84 | `readdir_batch(path, index, out, capacity)` | Copy up to 1–16 file-info records; return count, zero at end, or error; directory read/traverse permissions required |
| 85 | `window_frame(handle, operation, frame)` | Begin, present or cancel an owned generation-checked frame |
| 86 | `window_upload(handle, upload, pixels)` | Copy up to 1024 pixels into the matching pending frame |
| 87 | `window_input(handle, operation, out)` | QUERY=0 or one-way ENABLE=1; copied GUI input snapshot |
| 88 | `window_event2(handle, out)` | One GUI event; 1 delivered, 0 empty, error for invalid access |
| 89 | `clipboard(operation, buffer, length)` | QUERY=0, READ=1, WRITE=2; focused session owner only, up to 4096 bytes |
| 90 | `window_close_reply(handle, accept)` | 0 rejects, 1 closes/releases an owned pending close request |
| 91 | `auth_desktop(name, password)` | Boot login agent authenticates and starts the desktop session leader |
| 92 | `desktop(operation, value, entry)` | Session desktop display/panel queries and authorized window controls |
| 93 | `terminal_session(operation, value, buffer)` | Owned terminal endpoint creation, copied grid/state, input and resize |
| 94 | `session_logout()` | Authenticated shell/desktop ends the whole session |

`HackosTerminalSettings` contains six uint32 fields: pixel `fontSize`, RGBA
`bgColour`, RGBA `fontColour`, `columns`, `rows`, and zero `reserved`. The colours
are **0xRRGGBBAA**, unlike the kernel framebuffer's internal 0xAARRGGBB pixels.
`terminal_get` returns zero after a checked copy or `HACKOS_ERROR` for an invalid
destination. `terminal_set` keys are RESET=0 (value must be zero), FONT_SIZE=1
(even 8–64), BG_COLOUR=2 and FONT_COLOUR=3 (32-bit values). It checks ownership
first, returning `HACKOS_DENIED` for a non-owner, including root. Invalid keys,
values or sizes that leave no complete cell return `HACKOS_ERROR` without
mutation. Success returns zero after atomically updating geometry, resetting the
cursor and clearing the console. No syscall blocks or changes terminal ownership.
See [terminal appearance](utilities.md#terminal-appearance) for lifetime and alpha.

Calls use `int 0x80`: RAX is the number, RDI/RSI/RDX/RCX/R8 hold arguments,
and saved RAX receives the result. Successful operations without another result
return zero. `HACKOS_ERROR` (`UINT64_MAX`) denotes failure;
`HACKOS_DENIED` (`UINT64_MAX-1`) denotes insufficient permission.
`HACKOS_RETRY` (`UINT64_MAX-2`) asks the login agent to yield until the retry
deadline; `HACKOS_NO_ENTROPY` (`UINT64_MAX-3`) rejects password creation when
RDRAND is unavailable or repeatedly fails. `HACKOS_NOT_FOUND` (`UINT64_MAX-4`)
is returned by `stat` when a component is missing. Unknown and
retired calls return `HACKOS_ERROR`. The fast `syscall` instruction remains
disabled; vector `0x81` remains kernel-only.

[abi.h](../../include/hackos/abi.h) contains fixed-width, pointer-free output
records with pinned sizes and zeroed padding. `HackosCredentials` is 48 bytes
(UID, primary GID, supplementary count, zero reserved, eight GIDs); unused GIDs
are zero. User records append a 32-byte name (80 bytes total); group records
contain GID, zero reserved and a 32-byte name (40 bytes). The process record is
now 296 bytes: its original PID/state/name prefix is followed by UID/GID,
parent PID and process-group ID at offset 288. `HackosFileInfo` is now 296 bytes: its original 272-byte prefix
is followed by uint32 UID, GID, mode and metadataVersion (1), then uint32
`created` (offset 288) and `modified` (292). These are FAT date/time pairs:
date in the high 16 bits and time in the low 16 bits, with two-second precision
and no timezone. Zero/invalid dates are unknown; the FAT root has no date record. All file-info
and process-info consumers must be rebuilt with these layouts; old binary consumers are unsupported.
IDs accepted as syscall arguments are validated at full 64-bit width, so high
bits cannot alias root. Process states 4 and 5 mean blocked terminal input and
stopped, 6 I/O wait, 7 held startup, and 8 sleep; existing states retain their numbers. Unknown spawn flags are rejected before task creation.
Paths are NUL-terminated within 256 bytes; normalization accepts `/`, `~` or
a path relative to the caller's working directory. Copy-in validates mapped user pages; copy-out
also requires writable pages and validates the whole range before touching it.
Read validates the destination before advancing a file offset. Input events
include Up/Down (128/129), Left/Right (130/131), Delete (132), Ctrl+S (19),
Ctrl+X (24), Ctrl+Z (26), and `HACKOS_KEY_INTERRUPT` (3, protected shell/login prompt).
Both Ctrl keys produce the editor controls regardless of Shift/Caps Lock;
the decoded terminal API does not deliver release events. No syscall
returns kernel addresses, virtual objects, format strings or `va_list` objects.

Each process can own eight file handles. Monotonically allocated opaque tokens
are checked against the caller's live slots; closed, stale and foreign tokens
are rejected. Exit, fault and kill close all outstanding handles. Read handles
remain read-only; `open_mode` can request an exclusive writer. No other opener
may coexist with a writer. Path-based mutation rejects any path with a live handle
in any process, including unreaped zombies. Dispatcher serialization protects
this check and the operation; it does not yield inside its preemption guard.

`file_save` copies the entire user buffer before touching storage, rejects unknown
flags and oversize data, and checks allocation failure. Flag 0 requires an
existing regular file; flag 1 requires an absent destination. Empty saves are
supported. Invalid buffers, protected paths, read-only/open files, unsupported
operations, allocation failures and I/O errors return `HACKOS_ERROR`. There is
no cross-filesystem syntax or append flag. Descriptor writes and absolute seeks
use calls 55–58; permission failures may also return `HACKOS_DENIED`.

The editor and `xxd` reserve a 32 KiB private BSS buffer, check the file size,
read it completely using owned handles, and close before modifying it. The
editor rejects non-ASCII/binary contents (including CRLF), accepts LF and tabs,
and starts at the beginning of the file. Arrows, Backspace, Delete, Enter and
printable characters edit the buffer. A 72-column, 20-row viewport follows the
cursor, shown as `|`; tabs display as `>` while retaining their byte. Ctrl+S
saves even an empty new file and clears the modified marker only on success.
A failed save keeps the buffer and shows an error. Ctrl+X always exits; it does
not save or prompt, and reports discarded changes. Ctrl-C retains normal
foreground termination. Background editors fail without consuming terminal
input. Input overflow is reported so the user can inspect the buffer.

Mutations are synchronous and journaled together with their UID/GID/mode
metadata. See [filesystem persistence limits](filesystems.md).

## Runtime and tests

The active [libc Makefile](../../libs/libc/Makefile) builds startup assembly,
user-side printing/number formatting, shared memory/string/bitmap primitives,
and generated syscall wrappers. It excludes the older allocation, file-object,
and window helpers still present in that directory. The active public headers
are `hackos/user.h`, `hackos/syscall.h` and `hackos/abi.h`. The private heap and
formatting helpers are described above; shell buffers remain bounded stack/static storage.

Kernel and user code compile shared primitives with their own flags. Line editing
is shared with host tests through `common/include/hackos/runtime/line_buffer.h`.
All active programs use the isolated linker layout in `libs/ring3/userspace.ld`.
The test program reads a file through the safe ABI; the ring3 demo exercises
print, timer preemption and exit. The standalone `WINDOW` program ports the old
window demo onto owned handles and pixel uploads. Launch `./USR/BIN/WINDOW`,
then drag its title bar, resize its bottom-right grip, click to focus, type to
change the colour marker, and click the client to mark a position. Close X,
press Q, or use Ctrl-Alt-Escape. Use the title-bar `-` / `+` / `R` controls or keys M / F / R
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
area; the kernel adds a 30-pixel title bar. Normal client widths are 96–4096 pixels,
heights 30–2160, further bounded by the screen and window position. Coordinates start at 1 and the complete decorated window must
fit the screen. Titles must terminate within 30 bytes and contain printable
ASCII; long labels are visually clipped before the title-bar controls.

Each process may retain four handles, with a global limit of 16. Handles grant
access only to their owner; spawning a child does not transfer them. There is
no user mapping of window storage, framebuffer addresses or kernel objects.
Closed handles are rejected, and all retained handles are released by idle when
the process exits, faults or is terminated (including legacy Ctrl-C and Ctrl-Alt-Escape).

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
described below. The opt-in GUI input API below adds physical key transitions and close negotiation.

Idle updates mouse closure and flushes dirty drawing under a preemption guard.
The compositor overlays windows onto a RAM copy of the console and presents
changed spans to the physical display. Cursor-only motion reuses the composed
scene. Window pixels never overwrite the console backbuffer;
closing or reclaiming a window triggers redraw to restore the console beneath.

### Atomic frame submission and waits

The original `window_blit` remains available for legacy clients. New clients use:

| Slot | Call | Contract |
| --- | --- | --- |
| 85 | `window_frame(handle, operation, frame)` | BEGIN=0, PRESENT=1, CANCEL=2, BEGIN_RETAIN=3; copied readable/writable `HackosWindowFrame` |
| 86 | `window_upload(handle, upload, pixels)` | Copy at most 1024 packed pixels into the pending frame |

`HackosWindowFrame` is 16 bytes: `uint64_t generation`, then `uint32_t width,
height`. BEGIN allocates (or reuses) a black staging surface for current geometry,
invalidates any prior transaction and returns a new generation and dimensions.
BEGIN_RETAIN has the same allocation/generation contract, but initializes staging
from the last committed pixels. Newly exposed areas after resize are black;
uncommitted staging edits are never retained. Both begin operations keep the old
committed image visible. The first successful begin permanently
opts this handle into framed rendering; legacy blits then return `HACKOS_ERROR`.

`HackosWindowUpload` is 24 bytes: `uint64_t generation`, then `uint32_t x, y,
width, height`. Uploads validate the whole rectangle and copy its entire user
buffer before modifying staging. PRESENT swaps in staging as the committed
image and invalidates its changed bounds. Pixels not uploaded remain black after
BEGIN or retain their committed value after BEGIN_RETAIN.
CANCEL discards staging without modifying the visible image. PRESENT is atomic
with respect to composition, but is not a physical vertical-sync fence or a
promise that the monitor has already displayed the frame. The compositor keeps
its existing refresh pacing. Applications can tile directly from a small scratch
buffer; no full-screen userspace allocation is required.

Successful resize, another BEGIN, PRESENT or CANCEL makes old transaction
operations return `HACKOS_WINDOW_STALE`. Tokens must be used with their original
owned handle. Invalid pointers/rectangles/handles/operations return
`HACKOS_ERROR` without consuming a transaction. Allocation failure preserves all
existing state. Resize reserves its replacement before releasing old storage;
a successful resize therefore leaves staging available for the next BEGIN.
Repeated resizes keep only the last committed image and latest staging surface.

Existing syscall 72 `poll` also accepts `{handle, HACKOS_POLL_WINDOW |
HACKOS_POLL_READ, 0}`. READ reports queued window events or overflow; HANGUP
reports mouse closure; INVALID reports a foreign, released or invalid handle.
This does not consume events: use `window_event` after readiness. Other event
masks combined with WINDOW are invalid. Mix window entries and ordinary stream
entries within the existing 16-entry limit. Timeout 0 polls, finite milliseconds
expire with 0 ready entries, and `HACKOS_WAIT_FOREVER` blocks without a deadline.
Event arrival, overflow and closure wake waiters; predicate checks and blocking
are IRQ-atomic and never suspend inside a preemption guard. A standalone blocking
window wait is simply `poll` with one window entry.

### Window interaction and events

New windows take focus. A left click raises and focuses the topmost window under
the pointer; clicking outside windows restores terminal keyboard routing. The
focused title bar is blue. Closing the focused window selects the topmost
remaining visible window, or the terminal if none remain. Ownership remains enforced
regardless of focus: another process cannot query or consume your window events.

Drag the title bar to move, or the bottom-right 8-by-8 grip to resize. Geometry
stays within the display and 96–4096 by 30–2160 client limits. A successful
resize queues a resize event. Legacy clients receive a cleared buffer; framed
clients retain their last committed image, clipped to the new client area with
newly exposed space black, until they present a replacement. Allocation failure
leaves geometry, pixels, generation and any pending frame unchanged. Legacy
blits check current bounds; framed uploads additionally reject stale generations. Dragging/resizing captures the pointer until left-button release.
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
4096 by 2160 or below 98 by 90 are rejected. All front and staging surfaces, including temporary resize allocations, share
a 64 MiB page-rounded budget. Allocation also requires contiguous physical pages
in the supervisor identity map below 3 GiB; fragmentation or exhaustion returns
a controlled error. Window metadata uses a fixed 16-object pool. No surface is
allocated from the general kernel heap, and all surface pages are returned on
close or owner cleanup.
Successful maximize/normal restore invalidate frame generations and emit MOVE, RESIZE and MODE;
applications must repaint using the current geometry. Framed clients retain the old image meanwhile. Blits remain bounded to
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
is latched separately so a full queue cannot mask it. With legacy window focus Ctrl-C
terminates that window's owning process (and cleans all its windows), including
background owners. Opt-in GUI clients receive Ctrl-C as a key chord instead;
Ctrl-Alt-Esc explicitly terminates their focused owner. Without window focus it retains terminal foreground/cancel
semantics. Keyboard typeahead is cleared on focus/terminal ownership changes.

For two independent owners, launch `./USR/BIN/WINDOW &`, click the desktop,
then launch `./USR/BIN/WINDOW second &`. Switch focus by clicking their clients;
only the focused demo reacts to typed keys. Each demo paints small bounded
tiles, logs events, handles resize races, and recovers queue overflow. It processes
at most 16 events before giving repainting a turn, so pointer traffic cannot
keep a resized client black by preventing the queue from becoming empty. The demo submits complete generation-checked frames and retries after a resize;
between updates it blocks in `poll`, with a 16 ms retry delay on allocation failure.

Composition and validated pixel uploads use clipped row copies with independent
source/destination pitches. On x86-64 these copy whole 32-bit pixels, avoiding
per-pixel function calls and byte-at-a-time video-memory copies. Window borders
paint only their edges; the client is copied once instead of first being filled
with border colour. Idle coalesces dirty updates until at least 16 milliseconds after the previous
frame, using the configured PIT frequency. A pending final frame stays dirty
until displayed, even after uploads/input stop. Boot, fault and power-off paths
can still force an immediate draw. Composition still redraws the full display in RAM; damage bounds limit
comparison and video-memory writes. Frame submission is described below.

### Command display and directory listings

Successful process start/exit diagnostics go to the optional QEMU debug
port (0xe9) and COM1 serial log. They no longer enter the graphical console or scrollback. Command
output, nonzero exit status, Ctrl-C and actual faults/errors remain visible.

`ls` prints one entry per row and suffixes directories with `/`. By default it
hides dot-prefixed names and FAT hidden entries. `-a` includes those entries
(including `.` and `..` where supplied by the filesystem). `-l` adds a type
(`d` or `-`) and `rwx` mode, numeric UID/GID, FAT attribute flags (`r` read-only, `h` hidden, `s` system, `a`
archive; `-` for absent flags), byte size and name. Ownership comes from the
permission records; FAT attributes are shown separately. Timestamps remain
unexposed.
Use `-la`, `-al`, separate flags, or `--` to end option parsing. A single optional
path is supported; unknown flags, empty paths and extra paths report usage.

`terminal_cursor` rejects invalid values without changing caret state; an offset
before the visible console start is hidden. It does not move the output position
or consume input. `readdir_batch` validates the entire output range before I/O
and copies records only on success. Entry indices match `readdir`; calls are not
stable snapshots across directory mutations. The legacy single-record call remains
available. `User::BufferedOutput` offers an explicit 1 KiB stdout buffer; callers
must call `Flush()` and check its result. Failures remain sticky, and unrelated
`User::Print` calls retain immediate-output behavior.

### GUI input and desktop interaction

`window_input(handle, ENABLE, out)` opts one owned window into the GUI event
contract, clears its legacy queue and returns a copied state snapshot. Repeating
ENABLE is idempotent. QUERY requires prior opt-in. `window_event` then rejects
that handle; use `window_event2`. Legacy windows retain decoded press/repeat
keys, immediate mouse closure and window-owner Ctrl-C termination. The terminal's
decoded input, Ctrl-C/Ctrl-Z job control and existing stream API remain intact.

`HackosWindowEvent2` is 48 bytes: the original 32-byte event, `uint32_t modifiers,
flags`, and `uint64_t serial`. Geometry/pointer/focus fields keep their existing
meaning. The additional types are KEY_DOWN=9, KEY_UP=10, TEXT=11,
CLOSE_REQUEST=12 and INPUT_RESET=13. On key transitions, `event.key` is the
set-1 physical base code with bit 7 set for E0-prefixed keys, never a raw byte
stream. On TEXT it is a printable US-layout ASCII code point. Presses can emit
KEY_DOWN followed by TEXT; releases never emit text. Ctrl/Alt chords do not
emit text. Flags bit 0 marks typematic repeat. Modifiers are SHIFT=1, CTRL=2,
ALT=4, CAPS=8, combining left/right modifiers. Enter, Tab, arrows, Backspace,
Delete and Escape are physical-key events, not text. Pause/Print Screen and
unsupported sequences are ignored. Unicode, IMEs, alternative layouts and
Super-key shortcuts are not implemented.

`HackosWindowInputState` is 56 bytes: four `uint64_t keys` words, `uint64_t
serial`, then `uint32_t modifiers, buttons, focused, closePending`. Bit `key %
64` in word `key / 64` records an accepted held key. The serial is an input
reset epoch, separate from frame generations. On focus changes, queued GUI
input and held-key/button state are cleared, the epoch advances, and RESET then
FOCUS are delivered. Current modifiers are supplied on focus gain; key releases
can therefore arrive without a matching press in the new target. A key held
across a focus/reset boundary cannot start repeating actions there until a
fresh press. Losing focus cancels pointer capture. Keyboard-driven focus changes
suppress an already-held pointer button until release, preventing phantom clicks.

Window-queue overflow discards incomplete events and reports OVERFLOW; query
input, geometry and mode snapshots before continuing. Snapshots retain the
latest processed releases, so dropped release events cannot leave a client
stuck. Ingress overflow resets all GUI held state, cancels capture and suppresses
held pointer buttons until release. Modifier decoding remains authoritative.
Recovered Ctrl-C interrupts still reach the terminal/legacy windows; they never
invent GUI copy gestures or synthetic held keys. The emergency terminate chord
is latched independently of a full ingress queue.

Desktop shortcuts operate in idle, never in the IRQ:

| Shortcut | Action |
| --- | --- |
| Alt-Tab / Shift-Alt-Tab | Next/previous window in stable creation order; restore minimized targets |
| Alt-Esc | Give keyboard focus to the terminal |
| Alt-F4 | Request closure of the focused window |
| Alt-F9 | Minimize the focused window |
| Alt-F10 | Toggle maximize/normal geometry |
| Ctrl-Alt-Esc | Terminate the focused window's owner, reclaiming all its windows |

Reserved shortcut presses/repeats/releases are consumed by the manager. In GUI
windows Ctrl-C/Ctrl-X/Ctrl-V/Ctrl-A/Ctrl-S are ordinary application key chords,
not kernel editing commands. Applications handle widget Tab/Shift-Tab traversal,
selection, copy/cut/paste and saving. Terminal ownership does not automatically
change when Alt-Esc gives the terminal focus.

X and Alt-F4 queue a close request after preceding input. Repeated requests
coalesce; a separate pending latch survives overflow and focus changes. Poll
reports unread close requests as READ, not HANGUP. The application may save,
show a confirmation dialog, accept with `window_close_reply(handle, 1)` (which
also releases the handle), or reject with argument 0. Invalid arguments/pointers
never consume a request; replying without a pending request fails. Explicit
`window_close`, exit, fault, kill and logout still clean up immediately or through
the established reaper. A nonresponsive application can be terminated with
Ctrl-Alt-Esc. Close negotiation is application cooperation, not a forced-save
protocol.

The clipboard is an opaque byte payload of at most 4096 bytes. QUERY requires
null buffer/zero length and returns its size. READ returns the copied size;
insufficient capacity returns `HACKOS_BUFFER_SMALL` without a partial copy.
WRITE copies and validates the complete input before replacing contents;
writing zero bytes clears it. Invalid pointers, operations or oversize requests
leave contents unchanged. Access requires a nonzero authenticated session and
ownership of the currently focused window, including for root. Background or
foreign-session contents cannot be read. Descendants (including `runas`) share
the authenticated session's clipboard when focused; this is session scope, not
a per-UID clipboard. Session-controller termination/logout zeroes the payload.
No clipboard content is logged by the kernel.

`window --interaction` is the bounded interaction diagnostic: three 63-byte
ASCII fields with Tab/Shift-Tab and mouse focus, cursor movement, Backspace/Delete,
Ctrl-A selection and Ctrl-C/X/V. Ctrl-S saves all three LF-terminated fields to
`/tmp/gui-notes.txt`. Failed saves preserve edits. Closing with unsaved edits
shows Save and close / Discard / Cancel; Tab/Shift-Tab and Enter operate its
buttons, Escape cancels, and pointer clicks also work. The normal gradient demo
uses the same GUI input/close API. The reusable toolkit is documented in [Userspace GUI library](gui.md);
`window --widgets` exercises its drawing, layout, widgets and dialogs.

## Desktop sessions

See [desktop and independent terminals](desktop.md) for authenticated desktop
authority, taskbar controls, per-terminal input/output routing and foreground
jobs. Syscalls 91–94 add the desktop and terminal endpoint interfaces; existing
window, stream and recovery-console interfaces retain their contracts.

Account-management additions: syscall 95 `account_edit(op, id, value, name)` and
96 `password_change(old, new)` are documented in [Settings](settings.md).

Window terminal controllers can use terminal-session operation 5 to atomically
copy a live or historical viewport. See [the terminal contract](desktop.md#abi-and-authority)
for its 9672-byte record, row IDs, epoch, and controller-only authority. Existing
state and cell reads remain supported unchanged.

Terminal-session operation 8 adds an atomic styled viewport without changing
operation 5 or the existing text-cell records. See [ANSI output and ABI](desktop.md#ansi-output)
for supported sequences, attribute encoding and parser bounds. Redirected files
and pipes retain original bytes; parsing occurs only at a window-terminal output
endpoint. Recovery-console rendering remains unchanged.

## Bounded ICMP Echo

Syscall 97 exposes `echo(operation, HackosEcho*)` using the 24-byte record in
`include/hackos/network.h`. It is available to ordinary users, owns requests by
PID, never retains user pointers and does not grant raw packet access. Start,
query and cancel operations support one request per process, eight globally,
with three-second deadlines and ten-second completed-result retention. Process
reclamation cancels outstanding requests. See [networking](networking.md) for
status values, validation, rate limits, configured routing and the TTL/error-code union.


## Bounded DNS lookup

Syscall 98 exposes `resolve(operation, HackosResolve*)`, a copied 272-byte
record in `include/hackos/network.h`. START, QUERY and CANCEL follow the Echo
ownership pattern, with one lookup per PID, eight globally, five-second deadlines
and ten-second completed-result retention. Invalid QUERY output pointers retain
the result. Process reclamation releases entries. The resolver API exposes only DNS through the configured QEMU resolver;
separate connected UDP endpoints are described below. See
[networking](networking.md#udp-and-dns) for states and wire validation.


## Connected UDP endpoints

Syscall 99 `udp(operation, handle, HackosDatagram*)` uses a 544-byte copied record.
OPEN returns an exclusive, process-owned handle; SEND queues one complete payload,
STATUS reports transmission and queue state, RECEIVE copies then consumes one
whole datagram, and CLOSE releases it. Handles are never inherited or reused;
exit reclamation closes all owned endpoints. A failed receive copy preserves the
queued datagram. Sixteen endpoints globally, four per process, each have one
pending send and four received datagrams of up to 512 bytes. Calls are nonblocking;
userspace can sleep between retries. These handles are separate from file/pipe
descriptors and do not yet participate in `poll`. See
[networking](networking.md#connected-udp-endpoints) for ports, states and limitations.


DHCP changes require no new syscall. The hardware network member is now 120 bytes
inside the existing 128-byte record, with DNS/DHCP server addresses, state,
lease/remaining seconds, T1/T2 and attempt count appended after the old fields.
Configuration loss/change fails pending network work and clears UDP receive
queues; socket handles remain owned and valid. Identical-configuration renewals
preserve active traffic. See [DHCP](networking.md#dhcp-configuration-and-leases).

## Bounded TCP transactions

Syscall 100 `tcp(operation, HackosTcp*)` offers START/QUERY/CANCEL for one active
exchange/result per process (eight owned records globally). The copied record is
2072 bytes, with data at offset 24. START accepts 0–512 outgoing bytes and an IPv4
peer/port; localPort must be zero. QUERY returns up to 2048 received bytes,
completion/error state and retransmission count, without consuming the result.
CANCEL ignores its record pointer. Exit cancels owned work; the kernel retains
closed TIME-WAIT tuples separately from ownership for 120 seconds. Results expire
after ten seconds; pending work has a 20-second deadline. No descriptor or poll
integration is provided yet. See [networking](networking.md#bounded-tcp-exchanges)
for protocol limits and lease-change behavior.
