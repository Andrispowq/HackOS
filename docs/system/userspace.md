# Programs and userspace

[System index](index.md) · [Memory](memory.md) · [Interrupts](interrupts.md)

HackOS runs preemptive kernel threads, an interactive kernel shell, and minimal
isolated ring-3 programs. `user USR/BIN/RING3` launches the bundled print/exit
demo. The older `./` loader and libc applications remain trusted ring-0 code.

## Isolated ring-3 programs

[proc/user](../../kernel/src/proc/user/) loads static x86-64 ELF executables into
private physical frames. Each process owns its four page-table pages and up to
64 data/code/stack pages. Its 2 MiB virtual arena starts at
`0x0000400000000000`; the last 16 KiB is a zeroed, non-executable user stack,
preceded by an unmapped guard page. Kernel mappings are shared but supervisor-only.
Identical programs can run concurrently at the same virtual addresses.

The loader requires readable segments, rejects writable executable pages,
page-sharing segments, interpreters, dynamic linking and TLS, and caps ELF files
at 1 MiB. Code is read-only/executable; data and stacks are non-executable.
Empty load segments are allowed. BSS and page padding are zeroed. CPUs without
NX support cannot launch user programs. CR0.WP and EFER.NXE enforce permissions.
This path does not use the legacy page-table clone helpers.

The initial interrupt frame enters CPL3 with CS `0x1b`, SS `0x23`, IF enabled,
and IOPL zero. TSS.RSP0 selects the task's separate kernel stack on interrupts
and syscalls. Timer scheduling switches CR3 and restores the existing complete
register/FPU frame. Exit and ordinary user exceptions mark the task dead;
idle reclaims its kernel stack, private frames and page tables after switching
away. Kernel faults, NMI, double fault and machine check retain kernel handling.

The minimal ABI exposes only exit and bounded console writes. There is no user
heap, terminal input, file-handle API, dynamic linker, or standalone user shell
yet. The scheduler still runs on one CPU. This is a tested minimal isolation
boundary, not a hardened general-purpose sandbox.

## Trusted ring-0 ELF loading

[proc/elf/elf.cpp](../../kernel/src/proc/elf/elf.cpp) opens programs through the
first filesystem and checks for little-endian x86-64 ELF64 executable files.
`PrepareProgram` walks `PT_LOAD` segments, identity-maps their physical-address
fields through `KernelDirectory`, and copies file data to those addresses.

`program_task` calls the ELF entry point as a function with an `argc`/`argv`
pair. `LoadProgramTask` owns a copy of the path and permits one loaded ELF
program at a time, since the program linker scripts use the same addresses. The kernel startup smoke path also directly calls
the retained `USERTEST.ELF` entry.

The loader validates ELF version, header sizes, aligned program-header tables,
file-backed ranges, address overflow, segment alignment, non-overlap and an
executable entry point. It copies file bytes and zeros `p_memsz - p_filesz`.
Physical and virtual segment addresses must match this identity-loading model.
These checks do not provide isolation: destination ownership, segment permissions
and a process-owned address space are still needed before accepting untrusted
programs.

## Processes and scheduling

[Process](../../kernel/src/proc/tasking/process.cpp) owns its name and a 16 KiB
kernel stack. Kernel threads reference shared kernel page tables; user tasks additionally own
a private address space. `InitialiseTasking` creates the idle and shell tasks before IRQs
are enabled. Idle keeps the original boot stack; its first timer interrupt
captures its context.

Each new thread starts through a trampoline with SysV stack alignment and a
synthetic interrupt frame. Returning from the entry function terminates the
thread. IRQ0 saves the current frame and selects the next live task from a
null-terminated queue, wrapping to idle at the end. The interrupt epilogue
restores that task's frame. Vector `0x81` provides a ring-0 voluntary yield.
There is no compiler-frame-pointer walk. The scheduler switches CR3 when the
address space changes and updates the TSS kernel entry stack.

Exit marks the current task as a zombie and yields without returning. Killing
another task marks it as a zombie so it cannot be selected again. Idle unlinks
and deletes zombies only after execution has left their stacks. Stack and name
storage are freed once; private user pages are reclaimed and shared kernel
page tables stay alive. Program-owned file
handles, heap allocations and windows still require explicit cleanup by the
program; automatic resource tracking is future work.

The scheduler runs only on the bootstrap CPU. Nested `PreemptionGuard` scopes
protect shared heap, console, shell commands and program loading while allowing
hardware IRQ delivery. Queue operations use short interrupt-disabled sections.
Do not sleep, yield or exit inside a preemption guard. These guards are not SMP
locks. `SleepFor` waits on PIT ticks and does not remove a task from the ready queue.

## Interactive kernel shell

The shell thread consumes decoded PS/2 characters and refreshes the display when
console output changes. Keyboard IRQs only decode and enqueue input. The console
stays visible; the experimental window application in `libs/shell` is not started
automatically.

| Command | Behavior |
| --- | --- |
| `help`, `clear` | Show commands or clear the console |
| `print <text>` | Print literal text, including percent signs |
| `print tick`, `print time`, `print date` | Show the monotonic tick count or RTC |
| `ps`, `kill <pid>` | List tasks or stop a program; idle and shell are protected |
| `ls`, `wd`, `cd <path>` | Browse the first filesystem; `~` is its root |
| `open <path>`, `read`, `close` | Open a file, read up to 512 bytes, close it |
| `user <path>` | Launch an isolated ring-3 ELF, for example `user USR/BIN/RING3` |
| `./<program>` | Launch a trusted ELF program, for example `./USR/BIN/TEST` |
| `shutdown`, `restart` | Use the existing platform power-management paths |

Input uses the US keyboard layout, including Shift and Caps Lock. Backspace edits
the current line. Enter submits it. Lines longer than 255 characters are discarded;
a queue overflow also discards input and requires Enter before a fresh command.
There is no history, tab completion or interactive userspace terminal yet.
Power management and the example window programs remain hardware-dependent and
experimental.

## Syscalls

The kernel's [syscall implementation](../../kernel/src/arch/x86_64/syscall/)
and the userspace [kernel interface](../../libs/libc/src/kernel_interface/)
share numbered calls through `int 0x80`.

[include/hackos/syscalls.def](../../include/hackos/syscalls.def) is the single
catalogue of syscall numbers, argument types and kernel targets. It generates
the public declarations/numbers, common wrappers and kernel dispatch entries.
The wrappers and `MakeSyscall` assembly are compiled separately into the kernel
and libc. Existing numbers and register conventions are preserved; centralizing
the declarations alone does not validate pointers. The dispatcher selects a
restricted ABI when the saved CS indicates CPL3.

| Number | Operation |
| ---: | --- |
| 0 | Exit current task |
| 1 | Formatted kernel output |
| 2 | Console backspace |
| 3 | Open file |
| 4 | Read file |
| 5 | Write file |
| 6 | Close file |
| 7 | Allocate memory |
| 8 | Free memory |
| 9 | Read buffered input |
| 10 | Create window |
| 11 | Destroy window |
| 12 | Get window buffer |
| 13 | Set window title |
| 14 | Bounded literal console write |

`MakeSyscall` puts the call number in RAX and arguments in RDI, RSI, RDX, RCX,
and R8. The dispatcher uses the same registers and aligns the stack before
calling the C++ implementation. Results return through saved RAX; unknown syscall
numbers return `-1`.

Vector `0x80` is a DPL3 interrupt gate. CPL3 callers may use only 0 (exit,
status in RDI) and 14 (write, pointer in RDI and byte count in RSI). Writes
accept at most 1024 bytes, validate the entire mapped user range before copying,
and print literal bytes. They return the byte count or `-1`; zero-length writes
return zero. Calls 1–13 and unknown numbers return `-1` from ring 3. Vector
`0x81` remains kernel-only. The unfinished fast `syscall` path is disabled.

The legacy ring-0 API still passes raw pointers, `va_list`, and kernel objects.
Its kernel/libc `ActiveFile` declarations must match, including virtual method
order and fields. Host and guest tests retain coverage of that trusted ABI.
Future user file/window APIs need handles and per-process resource ownership.

## Runtime and programs

[libs/ring3](../../libs/ring3/) supplies a source-built standalone demo with its
own linker script and the shared syscall assembly stub. It does not link the
legacy libc.

[libs/libc](../../libs/libc/) supplies startup assembly, small C++ support
routines, syscall wrappers, and drawing helpers. `_start` aligns the stack and
calls `_entry`, which calls `main` and then requests exit. The linker scripts
place programs at `0x01000000`.

Kernel and libc share memory/string primitives, bitmap, vector/list helpers and
PSF font declarations through [common](../../common/). Allocation and output
remain component-specific. `split()` uses the same C implementation in BIOS
stage 2, the kernel and libc, with a local allocator adapter. It preserves empty
fields, including leading/trailing delimiters; empty input yields one empty
string. The result contains `count` separately allocated, terminated strings
and a null pointer sentinel. Callers free the strings and then the array using
their own allocator. Invalid input or allocation failure returns null and sets
`count` to zero when supplied. The BIOS bump allocator still cannot reclaim
memory (`kfree` is a no-op).

[libs/shell](../../libs/shell/) is a window/font experiment with much of its
interactive loop commented out. It is distinct from the
[kernel shell](../../kernel/src/shell/shell.cpp).
[libs/test](../../libs/test/) is an example application exercising arguments,
windows, and file I/O; it is not an automated test suite.

The [test suite](../development/testing.md) exercises ELF validation, register
preservation, shell input, task exit and reclamation on both firmware paths. The remaining
implementation work is prioritized in [TODO](../TODO.md).
