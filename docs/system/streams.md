# Standard streams, descriptors and waits

[Userspace](userspace.md) · [Filesystems](filesystems.md) · [Testing](../development/testing.md)

Step 18 gives each process 16 local descriptors. Descriptor 0 is stdin, 1 is
stdout and 2 is stderr. LOGIN initially receives terminal streams; ordinary
spawn calls inherit all open descriptors, including closed standard slots.
`User::Print`, `User::Number`, `printf` and the compatible `user_write` syscall
write stdout. `User::Error` writes stderr; `User::Write` handles bounded chunks
and reports failure. Command usage/error messages use stderr.

Descriptors refer to shared open descriptions. `dup2` and inheritance share the
file position, append flag or pipe endpoint. Closing one reference leaves the
others usable. Descriptor indices can be reused after close; they are not stale
handle detectors. The earlier eight opaque file tokens remain a separate,
non-inherited compatibility interface. Window handles also remain non-inherited.
Sharing a UID grants no access to another process's descriptor table. Inherited
open files are capabilities, including when root deliberately passes them to a
`runas` child; permissions are checked at open, not checked again on every byte.

## Shell syntax

```text
echo hello > /tmp/message
echo again >> /tmp/message
cat < /tmp/message | cat
cat /missing 2> /tmp/errors
echo hello | cat > "/tmp/two words"
runas guest cat /root-only 2>&1 | cat
```

The shell supports up to four stages and eight redirections per stage within
its existing 255-character command limit. Operators may touch words. Quotes
and backslashes preserve literal operators and empty arguments. `<` reads a
file; `>` creates or truncates; `>>` creates or appends. Optional descriptor
prefixes are 0, 1 or 2. `2>&1`, `1>&2` and other standard-descriptor duplications
copy the current mapping. Redirections apply left to right after pipeline
connections: `a >file 2>&1` puts both streams in the file; `a 2>&1 >file` leaves
stderr on the original stdout. A trailing `&` backgrounds the entire group.
Here-documents, descriptor-close syntax, arbitrary numbered redirections,
command substitution, expansion, `&&`, `||` and shell scripts are unsupported.

Pipeline creation holds each child until every stage has loaded and received
its final streams. Children receive only the selected descriptors 0–2, with
all other inherited slots closed. The shell closes its temporary pipe ends
before starting the group. Failed launches terminate and collect already-created
stages. Earlier file creation/truncation during redirection is not rolled back.
`jobs`, `fg`, `bg`, `wait`, Ctrl-C, Ctrl-Z and session teardown cover the whole
pipeline. Completion status comes from its last stage; all direct stage results
are collected together. Descendants remain in the group and delay completion.

State-changing shell built-ins require a single foreground command. They support
redirection with the shell's original streams restored afterward. They cannot
appear in pipelines or run in the background. `cat`/`read` accept an optional path;
without one they copy stdin to stdout. They preserve exact bytes and add no
newline. Tab completion recognizes pipeline and redirection boundaries and
escapes literal operators in filenames.

## Descriptor ABI

All records and buffers are copied and validated. Standard indices and limits
are defined in [abi.h](../../include/hackos/abi.h); wrappers come from
[syscalls.def](../../include/hackos/syscalls.def).

| Slot | Call | Result and contract |
| --- | --- | --- |
| 66 | `fd_open(path, mode)` | Lowest free descriptor; READ=1, WRITE=2, CREATE=4, TRUNCATE=8, APPEND=16 |
| 67 | `fd_read(fd, buffer, length, timeout_ms)` | Byte count; zero at EOF |
| 68 | `fd_write(fd, buffer, length, timeout_ms)` | Byte count, or error |
| 69 | `fd_close(fd)` | Zero; final writable-file close reports flush failure and still releases the descriptor |
| 70 | `dup2(source, destination)` | Destination index, replacing its previous reference; duplicating to self is valid |
| 71 | `pipe(uint64_t ends[2])` | Zero; copies read and write descriptors only after both are allocated |
| 72 | `poll(HackosPollFd* items, count, timeout_ms)` | Number of entries with returned events; zero on timeout |
| 73 | `wait_timeout(pid, status, flags, timeout_ms)` | Direct-child wait with a deadline |
| 74 | `job_wait_timeout(group, status, flags, timeout_ms)` | Whole-job wait with a deadline |
| 75 | `sleep(milliseconds)` | Zero after a finite delay; zero duration returns immediately |
| 76 | `spawn_io(path, argc, argv, HackosSpawnIo*)` | Creates a held member; group 0 starts a new group owned by the caller |
| 77 | `fd_seek(fd, absolute_offset)` | File position; pipes and terminal streams reject seeks |
| 78 | `fd_flush(fd)` | Zero for a successfully flushed file; other kinds reject it |
| 80 | `terminal_info(fd, HackosTerminalInfo*)` | Zero after copying terminal geometry/flags; valid files/pipes return an all-zero record; invalid descriptor or destination returns `HACKOS_ERROR` |

`HackosTerminalInfo` is 16 bytes: uint32 columns, rows, flags and a zero reserved
field. Flag 1 identifies a terminal; flag 2 reports that the caller may read
foreground input at the time of the query. Reading always rechecks permission.
The query consumes no input, exposes no pointers and requires no root privilege.
It lets pagers read keys separately from pipe/file stdin and avoid prompts in
redirected output. See [filter utilities](utilities.md#text-filters-and-pager).

`HackosSpawnIo` is 32 bytes: three uint64 descriptor indices followed by a uint64
group. `HACKOS_ERROR` in a stream slot requests a closed descriptor. A nonzero
group must be owned by the caller and still have a held member. `job_control`
FOREGROUND or BACKGROUND releases the held group; those operations retain the
terminal-owner authorization rule. A controller must start or terminate its
held groups. Controller exit terminates them automatically.

There are 128 shared descriptions and 16 pipes globally, independent of the
terminal descriptions. Transfers are at most 1024 bytes per call. Files retain
the 32 KiB write/seek bound, shared-read/exclusive-write rules, protected account
paths, traversal/UID/GID checks and journaled storage behavior. These checks
cover both descriptor and legacy-token interfaces. TRUNCATE and APPEND require
WRITE and cannot be combined. Append chooses the current shared file size on
each write. Copies and descriptor capacity checks precede file mutations.

Pipes contain 4096 bytes. Reads return available bytes; empty reads block while
any writer remains, then return EOF after all buffered bytes drain. Every
bounded pipe write is atomic: it waits for space for the entire request. A
write with no readers returns `HACKOS_BROKEN_PIPE`; no signal is generated.
Invalid output memory consumes no input, and invalid source memory writes no
bytes. Endpoint references are released at exit, fault or kill before idle
reclaims the process. Stopped processes retain their endpoints and buffers.
Files and other remaining resources are reclaimed by idle.

Terminal stream reads return decoded bytes without echo or line buffering.
The terminal owner and its foreground pipeline peers may read; background
reads return `HACKOS_DENIED`. Only stages created by `spawn_io` share this access;
ordinary children do not inherit it, even within the same process group. Explicit
foreground delegation to an ordinary child gives that child exclusive input.
Console clear/backspace and delegation still require the single terminal owner.
Keyboard typeahead is preserved when ownership passes between pipeline peers,
and cleared at other handoffs.
Ctrl-D returns EOF for that read. Queue overflow returns `HACKOS_RETRY` after
resetting the queue. The existing `terminal_read` API still reports decoded
key events and overflow separately; the interactive shell/editor use that API.
Terminal output streams write the graphical console, debug port and serial log.
Redirected output goes only to its destination; kernel diagnostics stay outside
process redirection.

## Blocking and deadlines

Timeouts are relative milliseconds: zero polls, `HACKOS_WAIT_FOREVER` waits
without a deadline, and finite values must fit uint32 (about 49.7 days).
`fd_read`/`fd_write` return `HACKOS_WOULD_BLOCK` for an immediate unavailable
operation, or `HACKOS_TIMED_OUT` when a finite wait expires. A child/job wait
with timeout zero returns TIMED_OUT if incomplete, unless WAIT_NOHANG requests
the existing zero result. Timeout and invalid status pointers never consume a
completion record. Stopped/interrupted job results keep their existing meanings.

`HackosPollFd` is 16 bytes: uint64 fd, uint32 events, uint32 revents. Up to 16
entries request READ=1 and WRITE=2. Returned HANGUP=4, ERROR=8 and INVALID=16
are reported even when not requested. A pipe can report READ and HANGUP
together until drained. A writer with no readers reports ERROR. File EOF is
read-ready; reading returns zero. Writable readiness means at least one byte
fits; a larger atomic write can still block. Poll never consumes bytes.
`poll(nullptr, 0, timeout)` waits solely for the deadline.

Wait predicates and transitions are atomic with interrupts masked. Pipe/input
changes wake blocked I/O tasks to recheck their predicates; timer scheduling
wakes expired waits. No blocking operation holds a PreemptionGuard. New process
states are IO=6, STARTING=7 and SLEEP=8; older state numbers remain unchanged.
Stopped waits resume by rechecking their original deadline. Timeouts bound
scheduler waits, not synchronous disk/controller operations or flushes.

## Serial diagnostics

Both C loaders initialize COM1 before their own initialization work. Kernel
entry initializes it again before display setup and records BIOS/UEFI. COM1
uses 115200 baud, 8N1, polled transmission and no UART interrupts. Detection and
transmit waits are bounded; an absent/unresponsive UART disables serial output
without blocking boot. This starts at BIOS stage-2 C entry, after the real-mode
video/memory setup; MBR/stage-1 diagnostics remain outside its coverage.

The kernel mirrors terminal text and faults to serial, while lifecycle messages
remain off the graphical console and go to debug/serial logs. Password entry
is not echoed or logged. Capture output with:

```sh
python3 tools/run_qemu.py --firmware bios --image out/bios/HackOS.img --serial-log out/bios/serial.log
python3 tools/run_qemu.py --firmware uefi --image out/uefi/HackOS.img --serial-log out/uefi/serial.log
```

Serial is an output diagnostic channel, not another login/input terminal.
EOF, broken pipes and terminal control remain HackOS interfaces, not a claim
of POSIX compatibility.
