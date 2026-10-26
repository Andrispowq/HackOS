# Programs and userspace

[System index](index.md) · [Memory](memory.md) · [Interrupts](interrupts.md)

HackOS boots directly into the standalone ring-3 shell at `~/USR/BIN/SHELL`.
`InitialiseKernel` initializes filesystems and syscalls, creates idle, loads the
shell, then enables IRQs. The first scheduling pass enters userspace. A missing
or invalid shell produces an explicit boot error; there is no kernel-shell
fallback. `kernel_main` remains idle, refreshes the console and reaps dead tasks.

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
orphans and are still reclaimed by idle. No PID is reused during a boot.

The old trusted ELF helpers remain internal code. The binary-only `USERTEST.ELF`
asset is retained but no longer called during boot. Neither shell launch syntax
can execute a program in ring 0. Old fixed-address libc/window binaries are
incompatible with the active ABI and must be ported and rebuilt.

## Standalone shell

[libs/shell/src/main.cpp](../../libs/shell/src/main.cpp) owns command parsing,
line editing, working-directory state, formatting and its current file handle.
Keyboard IRQs only decode and enqueue characters. The boot shell reads those
characters through the input syscall and yields when the queue is empty.
Console rendering and hardware operations remain kernel services.

| Command | Behavior |
| --- | --- |
| `help`, `clear` | Show commands or clear the console |
| `print <text>` | Print literal text, including percent signs |
| `print tick`, `print time`, `print date` | Show monotonic ticks or RTC values |
| `ps`, `kill <pid>` | List tasks or stop a user program; idle and boot shell are protected |
| `ls`, `wd`, `cd <path>` | Browse the first filesystem; `~` denotes its root |
| `open <path>`, `read`, `close` | Open a read-only handle, read up to 512 bytes, close it |
| `./<program> [args]`, `user <path> [args]` | Launch an isolated foreground program and wait; append a separate `&` for background execution |
| `shutdown`, `restart` | Request kernel ACPI power-off or platform restart |

Input supports the US layout, Shift, Caps Lock and Backspace. Lines longer than
255 characters are discarded. Queue overflow discards input until Enter. There
is no history, completion, pipelines, environment expansion, Ctrl-C/job control
or blocking terminal API yet. Program output shares the console with the shell.

Program commands split arguments on spaces/tabs. Single or double quotes group
words (including empty strings); a backslash escapes the next character, also
inside quotes. Unmatched quotes, a trailing backslash or excessive arguments
reject the command. This syntax applies to program launches; existing built-in
commands retain their literal text/path handling. The shell resolves `argv[0]`
to the executable path. For example:

```text
./USR/BIN/TEST alpha "two words" ""
user USR/BIN/RING3
./USR/BIN/TEST background &
```

The foreground prompt appears only after wait returns. Nonzero statuses are
reported. While waiting, the shell does not consume queued keyboard input or
transfer its terminal privilege to the child. A bare trailing `&` opts out of
waiting; the shell collects background results with nonblocking waits during
its input loop.

Only the process designated by the kernel at boot owns terminal input, console
editing, task-kill and power privileges. Spawning another copy of SHELL does not
inherit those privileges. Ordinary programs can print, inspect processes, read
files, spawn, yield and exit. This is a minimal single-user permission model,
not a hardened multi-user security system. Power returns zero when the hardware request is issued, or an error when it
cannot be issued. Actual completion is confirmed by guest shutdown, not just a
return value or printed message.

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
| 15 | `input()` | Character, 0 if empty, 256 for queue overflow; boot shell only |
| 16 | `console(operation)` | 0 backspace, 1 clear; boot shell only |
| 17 | `clock(out)` | Copies ticks and RTC fields; RTC retains existing BCD representation |
| 18 | `process_info(index, out)` | Copies one process record; 1 found, 0 end |
| 19 | `kill(pid)` | Stops another user task; boot shell only |
| 20 | `spawn(path)` | Copies a bounded path and loads an isolated ELF; returns PID |
| 21 | `open(path)` | Returns an opaque read-only file handle |
| 22 | `read(handle, out, length)` | At most 512 bytes; returns bytes read, 0 at EOF |
| 23 | `close(handle)` | Closes a handle owned by the caller |
| 24 | `stat(path, out)` | Copies file size, attributes and name |
| 25 | `readdir(path, index, out)` | Copies one directory entry; 1 found, 0 end |
| 26 | `power(operation)` | 0 ACPI shutdown, 1 restart; boot shell only |
| 27 | `yield()` | Gives another runnable task a scheduling opportunity |
| 28 | `spawn_args(path, argc, argv)` | Copies a bounded user pointer vector and strings; returns child PID |
| 29 | `wait(pid, status, flags)` | Collects a direct child; flags 0 block, `HACKOS_WAIT_NOHANG` polls |

Calls use `int 0x80`: RAX is the number, RDI/RSI/RDX/RCX/R8 hold arguments,
and saved RAX receives the result. Successful operations without another result
return zero. `HACKOS_ERROR` (`UINT64_MAX`) denotes failure;
`HACKOS_DENIED` (`UINT64_MAX-1`) denotes insufficient permission. Unknown and
retired calls return `HACKOS_ERROR`. The fast `syscall` instruction remains
disabled; vector `0x81` remains kernel-only.

[abi.h](../../include/hackos/abi.h) contains fixed-width, pointer-free output
records with pinned sizes and zeroed padding. Paths are NUL-terminated within
256 bytes and start with `~`. Copy-in validates mapped user pages; copy-out
also requires writable pages and validates the whole range before touching it.
Read validates the destination before advancing a file offset. No syscall
returns kernel addresses, virtual objects, format strings or `va_list` objects.

Each process can own eight file handles. Monotonically allocated opaque tokens
are checked against the caller's live slots; closed, stale and foreign tokens
are rejected. Exit, fault and kill close all outstanding handles. This version
has no user file-write or window API: those need explicit safe interfaces rather
than access to the old kernel object layout.

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
print, timer preemption and exit. The old split adapters retain host coverage,
but allocation-dependent legacy libc helpers are not linked into user programs.

The [testing guide](../development/testing.md) covers shell CPL3 entry, input,
file handles, bad pointers, concurrent programs, register preservation, resource
cleanup and actual guest ACPI shutdown on both BIOS and UEFI. Further work is
listed in [TODO](../TODO.md).
