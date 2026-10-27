# Output, clock and hardware utilities

[System index](index.md) · [Userspace](userspace.md) · [Devices](devices.md)

These standalone ring-3 programs use inherited stdout/stderr. They work in
pipelines, in the background and with file redirection. Exit status is 0 on
success, 1 for an unavailable service/denied operation/output failure, and 2 for
invalid usage. New images package `ECHO`, `TIME` and `SYSINFO`; `PRINT` is retired.

## Literal output

```sh
echo hello world
echo "two  spaces" tick time date
echo -n "no newline" > /tmp/message
```

`echo` joins arguments with one space and appends a newline. Leading `-n` operands
suppress that newline. An empty invocation emits just a newline. Percent signs,
`tick`, `date`, `time`, `--` and unknown options are literal; backslash escape
interpretation (`-e`) is not provided. The shell's own quote/backslash decoding
happens before execution. This follows the basic Unix
[echo convention](https://www.gnu.org/software/coreutils/manual/html_node/echo-invocation.html)
with a deliberately small option set.

## Clock

```sh
time                      # 2026-10-27 09:08:07 | 12345 ticks
time --date               # 2026-10-27
time --time               # 09:08:07
time --ticks              # 12345
time -d -t                # date and time, from one sample
time --all > /tmp/clock
```

`-d/--date`, `-t/--time` and `-k/--ticks` combine in a fixed date/time/ticks order;
`-a/--all` selects all fields. `-h/--help` describes the command. Calendar fields
are zero-padded, and the tick-only form contains just an unsigned decimal count.
The RTC has one-second resolution and supplies its own wall-clock setting;
HackOS has no timezone database, synchronization service or timezone conversion.
Ticks count delivered PIT interrupts since kernel timer initialization, not CPU
cycles. `sysinfo system` reports the configured nominal tick frequency.

The kernel obtains two matching samples outside an RTC update, with bounded
retries. It handles binary/BCD and 12/24-hour modes, validates the calendar and
uses the FADT century register when supplied; without one, years mean 2000–2099.
An unavailable/unstable RTC returns an error. FAT timestamps use the same decoded
clock, with the FAT epoch as their fallback if it cannot be read.

The 16-byte `HackosClock` ABI now contains ticks (uint64), second/minute/hour/day/
month/reserved bytes, and a full uint16 year at offset 14. All calendar fields are
binary; month is 1–12. Old programs that interpreted BCD must be rebuilt.
The command retains the requested name `time`: its purpose is closer to Unix
[`date`](https://www.gnu.org/software/coreutils/manual/html_node/date-invocation.html)
and uptime reporting than to timing another command.

## Hardware reports

```sh
sysinfo
sysinfo memory
sysinfo pci
sysinfo usb
sysinfo ps/2
sysinfo full | cat > /tmp/hardware.txt
```

`sysinfo [section]` defaults to `full`. The structure follows the hardware-class
reporting idea of [`lshw`](https://ezix.org/project/wiki/HardwareLiSter), using
subcommands instead of its Linux probing interfaces. `--help` is available to
anyone; **every data query requires UID 0 in the kernel**. Access does not depend
on the executable's filename or file permissions.

| Section | Contents |
| --- | --- |
| `full` | All the sections below, in a stable order |
| `system` | Architecture, BIOS/UEFI boot mode, ACPI OEM, nominal PIT rate |
| `cpu` | CPUID vendor/brand, family/model/stepping, feature bits, discovered ACPI CPUs and active scheduler CPU count |
| `memory` | Firmware-usable RAM, allocated/free frames, reserved address span, page size and map entry count |
| `memory-map` | Every firmware region's physical start/size/type and allocator usability; E820/EFI type namespace follows the boot mode |
| `disk` | Registered ATA PIO/AHCI devices, sector count/capacity and FAT32 boot-device indication |
| `pci` | Cached segment:bus:device.function addresses, vendor/device IDs, class/subclass/interface and revision |
| `usb` | USB-class PCI controllers with interface and driver status |
| `ps2` | Keyboard IRQ registration/layout, mouse enable result and negotiated packet/wheel protocol; aliases `ps/2`, `pc/2` |
| `display` | Boot framebuffer dimensions, bit depth and scanline pitch |
| `acpi` | Root table entries with signatures, revisions, sizes and OEM identifiers |

Reports use aligned ASCII headings/columns without terminal escape sequences.
Memory is displayed in MiB and exact bytes. The memory-map usability flag identifies
firmware regions eligible for allocation before kernel reservations; it is not a
live per-page free/used bitmap. Reserved address span includes holes
and must not be added up as installed DIMM capacity. Disk registry entries may
alias the same physical disk. No model/serial, free disk-space, DIMM/SPD, thermal,
network-stack, GPU-acceleration or USB attached-device information is invented
when the kernel does not collect it. More discovered CPUs do not imply SMP is
active; table/controller presence does not imply a working driver.

### Hardware ABI

Call 79, `hardware(section, index, out)`, copies a zero-initialized 128-byte
`HackosHardwareInfo` union from `include/hackos/hardware.h`. It returns 1 for a
record, 0 at end, `HACKOS_DENIED` for non-root, and `HACKOS_ERROR` for an invalid
section or invalid writable destination. Authorization precedes index/pointer
handling. Singletons have only index 0; disks, PCI functions, ACPI tables and
memory-map entries enumerate from zero. No record contains a kernel pointer.

Each call is a bounded snapshot under the scheduler's preemption guard. A full
report uses several calls, so RAM counters may change between sections. The
query path performs no hardware writes, allocation, blocking or kernel console
printing. PCI stores at most 512 functions and reports dropped entries explicitly;
missing discovery is distinguished from an empty inventory. The memory map and
ACPI information remain owned by the existing boot/discovery components.
