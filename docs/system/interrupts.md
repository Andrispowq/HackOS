# Interrupts and time

[System index](index.md) · [Devices](devices.md) · [Programs](userspace.md)

The kernel owns its GDT and IDT and uses the legacy 8259 PIC for hardware IRQ
routing. ACPI APIC discovery exists, but it is separate from the interrupt path
used by the timer, keyboard, and mouse.

## GDT and TSS

[gdt.cpp](../../kernel/src/arch/x86_64/gdt/gdt.cpp) installs kernel code/data,
user code/data, and TSS entries. Kernel code/data selectors are `0x08`/`0x10`;
the user descriptors occupy `0x18`/`0x20`. The 64-bit TSS descriptor starts at
`0x28` and occupies two slots, including its upper address and reserved fields.

The scheduler updates TSS.RSP0 to the selected task's kernel stack. User tasks
enter with selectors `0x1b`/`0x23` and IOPL zero. The I/O bitmap offset lies
beyond the TSS limit, denying user port access. Only vector `0x80` permits a
user software interrupt; `0x81` remains kernel-only.

## Exception entry and dispatch

[interrupts.asm](../../kernel/src/arch/x86_64/interrupts/interrupts.asm) supplies
the entry stubs. Their saved frame must match the 16-byte-aligned `Registers` structure
in [idt.h](../../kernel/src/arch/x86_64/interrupts/idt.h), including general
registers, a 512-byte FXSAVE area, vector/error fields, and the CPU return frame.
The full frame is 704 bytes; static assertions pin important offsets.

[idt.cpp](../../kernel/src/arch/x86_64/interrupts/idt.cpp) installs gates and a
handler table. `RegisterInterruptHandler` associates a vector with a C++
callback; `ISRHandler` dispatches exceptions/software interrupts and
`IRQHandler` dispatches hardware IRQs and sends end-of-interrupt notifications
exactly once before selecting the next task on IRQ0. Both handlers return the
frame pointer for the assembly epilogue to restore. Vector `0x81` yields without
sending a PIC notification.

Page-fault reporting lives in [paging.cpp](../../kernel/src/arch/x86_64/paging/paging.cpp).
`InitPaging` registers `PageFaultHandler` through the callback table, preserving
the vector-14 assembly entry. `fxsave64`/`fxrstor64` preserve x87 registers and
control state, MXCSR and XMM0–15. The FXSAVE area and C++ call stack are aligned
to 16 bytes. `cld` makes C++ handlers safe when an interrupted task had DF set;
`iretq` restores that task's flags, including DF and the interrupt-enable bit.
Error-code and no-error-code vectors use matching stubs. AVX/XSAVE state is not
supported by this context format.

## Hardware IRQs

The PIC IRQ range is remapped to vectors 32–47. Initialization configures the
drivers before unmasking their IRQs and executing `sti`.

| IRQ | Vector | Consumer |
| --- | ---: | --- |
| IRQ0 | 32 | PIT timer |
| IRQ1 | 33 | PS/2 keyboard |
| IRQ2 | 34 | Cascade to the slave PIC |
| IRQ12 | 44 | PS/2 mouse |

An IRQ handler runs in interrupt context. Long operations, allocations, shell
commands, or waiting for other interrupts complicate reentrancy and scheduling.
The keyboard path only decodes scancodes and queues characters; the shell thread
handles commands and display updates.

## PIT and RTC

[pit.cpp](../../kernel/src/arch/x86_64/timer/pit.cpp) programs the PIT at 1,000 Hz
during normal initialization. The callback increments a volatile 64-bit `tick`.
The IRQ dispatcher acknowledges the PIC and then calls `Schedule`; the counter
is never reset by the shell.

`SleepFor` and `SleepForSeconds` wait for timer progress with `hlt`; they are not
a scheduler-backed sleep queue. They depend on timer interrupts continuing to
arrive. [rtc.cpp](../../kernel/src/arch/x86_64/timer/rtc.cpp) reads calendar time
from CMOS and supplies dates/times used by the shell and FAT metadata.

## Assembly contracts to preserve

Task switching consumes the same interrupt frame as ordinary interrupt return.
Changing its layout, stack alignment or FPU state requires coordinated C++ and
NASM changes. The scheduler does not depend on compiler frame-pointer walking.
QEMU's configured core count does not mean the kernel has started or scheduled
work on secondary processors.

The [guest regression](../development/testing.md) checks general registers,
XMM0–15, x87 state, MXCSR and flags while actual task switches occur. Ring-3
transitions and contained protection faults are covered by `qemu_user.py`.
AVX state remains unsupported.
