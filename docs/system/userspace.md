# Programs and userspace

[System index](index.md) · [Memory](memory.md) · [Interrupts](interrupts.md)

HackOS has the pieces of a program runtime, but it does not yet run isolated
userspace processes. Tasking startup is disabled, new processes select kernel
privilege, and program segments are loaded through the kernel address space.

## ELF loading

[proc/elf/elf.cpp](../../kernel/src/proc/elf/elf.cpp) opens programs through the
first filesystem and checks for little-endian x86-64 ELF64 executable files.
`PrepareProgram` walks `PT_LOAD` segments, identity-maps their physical-address
fields through `KernelDirectory`, and copies file data to those addresses.

`program_task` calls the ELF entry point as a function with an `argc`/`argv`
pair, prints its result, and kills the current task. `LoadProgramTask` wraps that
function in a new `Process`. The kernel startup smoke path also directly calls
the retained `USERTEST.ELF` entry.

This loader needs file-bound checks, overflow checks, address-range validation,
segment permission handling, and correct zeroing of `p_memsz - p_filesz` for
BSS. It currently clears only `p_filesz` bytes before copying the same number
of bytes. Programs cannot safely choose arbitrary physical destinations.

## Processes and scheduling

[Process](../../kernel/src/proc/tasking/process.cpp) holds register/stack state,
a page-table reference, a name/PID, state, and a link to another process.
Construction prepares an interrupt-return frame and clones the current page
tables, but `isUser` is hard-coded to `false`.

`InitialiseTasking` relocates the initial stack and creates idle, confirmation,
and kernel tasks. Its call in kernel initialization is commented out. The PIT
can call `Schedule` once scheduling is enabled, and
[tasking.asm](../../kernel/src/proc/tasking/tasking.asm) saves/restores context
and switches CR3.

The task-switch code assumes compiler frame pointers and a specific interrupt
stack shape. It also restores the XMM11 slot into XMM12, leaving XMM11 unrestored;
this needs fixing before relying on context preservation. Process termination
and page-table ownership need review before enabling general scheduling.

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
and R8. The dispatcher reads its fifth argument from RBX instead of R8; that
mismatch must be resolved before five-argument calls are added. Results return
through the saved RAX value.

Vector `0x80` currently uses the ordinary privilege-0 interrupt-gate flags, so
it is not a usable ring-3 entry point. `SetupSysret` configures some MSRs, but
the wrappers still use `int 0x80`; a complete `syscall`/`sysret` path is not wired
up. The assembly `JumpToUserspace` helper is preparatory rather than the normal
execution path.

The API passes raw pointers, `va_list`, and kernel objects across the boundary.
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

The [testing plan](../development/testing.md) starts with ELF validation and
context-switch checks before adding ring-3 isolation tests. The remaining
implementation work is prioritized in [TODO](../TODO.md).
