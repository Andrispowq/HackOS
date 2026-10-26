# Programs and userspace

[System index](index.md) · [Memory](memory.md) · [Interrupts](interrupts.md)

HackOS runs preemptive kernel threads and an interactive kernel shell. All tasks
remain in ring 0 and share `KernelDirectory`; there is no isolated userspace yet.
Only trusted programs should be loaded.

## ELF loading

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
kernel stack. It references the shared kernel page tables without owning or
cloning them. `InitialiseTasking` creates the idle and shell tasks before IRQs
are enabled. Idle keeps the original boot stack; its first timer interrupt
captures its context.

Each new thread starts through a trampoline with SysV stack alignment and a
synthetic interrupt frame. Returning from the entry function terminates the
thread. IRQ0 saves the current frame and selects the next live task from a
null-terminated queue, wrapping to idle at the end. The interrupt epilogue
restores that task's frame. Vector `0x81` provides a ring-0 voluntary yield.
There is no compiler-frame-pointer walk or CR3 switch.

Exit marks the current task as a zombie and yields without returning. Killing
another task marks it as a zombie so it cannot be selected again. Idle unlinks
and deletes zombies only after execution has left their stacks. Stack and name
storage are freed once; the shared page tables stay alive. Program-owned file
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

`MakeSyscall` puts the call number in RAX and arguments in RDI, RSI, RDX, RCX,
and R8. The dispatcher uses the same registers and aligns the stack before
calling the C++ implementation. Results return through saved RAX; unknown syscall
numbers return `-1`.

Vector `0x80` currently uses the ordinary privilege-0 interrupt-gate flags, so
it is not a usable ring-3 entry point. `SetupSysret` configures some MSRs, but
the wrappers still use `int 0x80`; a complete `syscall`/`sysret` path is not wired
up. The assembly `JumpToUserspace` helper is preparatory rather than the normal
execution path.

The API passes raw pointers, `va_list`, and kernel objects across the boundary.
In particular, the kernel and libc `ActiveFile` declarations must have matching
virtual destructors, method order and fields. A cross-translation-unit host test
and a guest file-I/O probe exercise this ABI.
Actual userspace support needs pointer validation/copying, file and window
handles, per-process resources, a kernel entry stack, and an explicit error ABI.

## Runtime and programs

[libs/libc](../../libs/libc/) supplies startup assembly, small C++ support
routines, syscall wrappers, and drawing helpers. `_start` aligns the stack and
calls `_entry`, which calls `main` and then requests exit. The linker scripts
place programs at `0x01000000`.

[libs/shell](../../libs/shell/) is a window/font experiment with much of its
interactive loop commented out. It is distinct from the
[kernel shell](../../kernel/src/shell/shell.cpp).
[libs/test](../../libs/test/) is an example application exercising arguments,
windows, and file I/O; it is not an automated test suite.

The [test suite](../development/testing.md) exercises ELF validation, register
preservation, shell input, task exit and reclamation on both firmware paths. The remaining
implementation work is prioritized in [TODO](../TODO.md).
