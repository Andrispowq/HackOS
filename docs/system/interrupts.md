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

The presence of user descriptors is preparatory. The TSS stack initialization
is commented out, and normal process construction selects kernel privilege.
Ring transitions require a correct 64-bit TSS descriptor, kernel entry stack,
and interrupt stack behavior before these descriptors can be used safely.

## Exception entry and dispatch

[interrupts.asm](../../kernel/src/arch/x86_64/interrupts/interrupts.asm) supplies
the entry stubs. Their saved frame must match the packed `Registers` structure
in [idt.h](../../kernel/src/arch/x86_64/interrupts/idt.h), including general
registers, XMM storage, vector/error fields, and the CPU return frame.

[idt.cpp](../../kernel/src/arch/x86_64/interrupts/idt.cpp) installs gates and a
handler table. `RegisterInterruptHandler` associates a vector with a C++
callback; `ISRHandler` dispatches exceptions/software interrupts and
`IRQHandler` dispatches hardware IRQs and sends end-of-interrupt notifications.

Page-fault reporting lives in [paging.cpp](../../kernel/src/arch/x86_64/paging/paging.cpp).
`InitPaging` registers `PageFaultHandler` through the callback table, preserving
the vector-14 assembly entry. XMM saves use unaligned moves, the C++ call stack
is aligned for SysV, and XMM11 is restored to its own register. `iretq` restores
the interrupted interrupt-enable state; the epilogue does not enable interrupts
early. Error-code and no-error-code vectors use matching stubs.

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
The current keyboard path calls command handling directly; moving that work to
an input queue is a useful next step.

## PIT and RTC

[pit.cpp](../../kernel/src/arch/x86_64/timer/pit.cpp) programs the PIT at 1,000 Hz
during normal initialization. The callback increments `tick` and
`timeSinceBoot`. If scheduling is enabled, it acknowledges the PIC and switches
tasks through `Schedule`.

`SleepFor` and `SleepForSeconds` wait for timer progress with `hlt`; they are not
a scheduler-backed sleep queue. They depend on timer interrupts continuing to
arrive. [rtc.cpp](../../kernel/src/arch/x86_64/timer/rtc.cpp) reads calendar time
from CMOS and supplies dates/times used by the shell and FAT metadata.

## Assembly contracts to preserve

Task switching also consumes the interrupt register frame and walks compiler
frame pointers. Changing the layout, stack alignment, XMM save/restore code,
optimization, or frame-pointer flags requires reviewing both interrupt and
task-switch assembly. QEMU's configured core count does not mean the kernel
has started or scheduled work on secondary processors.

See [TODO](../TODO.md) for interrupt-entry and task-switch repairs.
