# Command utilities

[System index](index.md) · [Userspace](userspace.md) · [Devices](devices.md)

These standalone ring-3 programs use inherited stdout/stderr for pipelines and
file redirection. Interactive operations and appearance changes require foreground
access, as described below. Exit status is 0 on
success, 1 for an unavailable service/denied operation/output failure, and 2 for
invalid usage, except for the grep statuses described below. `PRINT` is retired in favour of `ECHO`.

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

## Text filters and pager

`grep`, `wc`, `head`, `tail`, `tee`, `more`, `sort`, `uniq`, `cut` and `tr` are standalone programs available
to ordinary users. File access uses the caller's existing permissions. All use
stdin/stdout/stderr, support `--help` and `--`, and put diagnostics on stderr.
Options precede operands; unknown options fail instead of being ignored.
Omitted input files or a `-` input operand select stdin. Grep/wc/head/tail/more/cut
process several files in order; sort combines their lines. Uniq accepts one input
and an optional output file (`-` means stdout). Tr accepts sets rather than files
and always reads stdin. Tee always reads stdin and treats `-` as a literal filename.

```sh
cat /usr/hello.txt | grep -in hello | tee /tmp/matches | wc -l
grep '^row[0-9]*$' /tmp/log | head -n 20
tail -n 10 < /tmp/log
head -c 32 /tmp/data > /tmp/prefix
cat /tmp/log | more
echo another line | tee -a /tmp/log
cut -d: -f1 /tmp/records | tr A-Z a-z | sort | uniq -c
cut -s -d: -f2 /tmp/records | sort -nr > /tmp/numbers
tr -d '[:digit:]' < /tmp/text | tr -s '[:space:]'
```

| Command | Supported options and behavior |
| --- | --- |
| `grep [options] pattern [file ...]` | Basic pattern matching; `-F` literal, `-i` ASCII case-insensitive, `-n` line numbers, `-v` inverse, `-c` selected-line counts, `-l` matching filenames, `-q` quiet/early success, `-H` always prefix filename, `-h` suppress filename, one `-e pattern` for patterns starting with `-` |
| `wc [-lwc] [file ...]` | Newlines, words, bytes, in that order; defaults to all three, totals for multiple operands. ASCII whitespace separates words. A final partial line contributes words/bytes but no newline |
| `head [-n N] [-c N] [-qv] [file ...]` | First N lines or bytes; default ten lines. `-q` suppresses multi-file headers, `-v` always adds headers. Zero emits nothing and does not consume stdin |
| `tail [-n [+]N] [-c [+]N] [-qv] [file ...]` | Last N lines/bytes; default ten lines. `+N` starts at the one-based line/byte N (0 also means the beginning). Header flags as for head. Zero without `+` drains input and emits nothing |
| `tee [-a] [file ...]` | Copy stdin to stdout and every file; create/truncate by default, `-a` appends. Reports open/write/close/flush failures and continues surviving outputs. A broken stdout pipe ends the command |
| `more [-n lines] [file ...]` | Page foreground terminal output; default one screen minus one row. Space advances a page, Enter one row, q quits; Ctrl-Z/C use normal job control. `-n` accepts 1–10000 rows |
| `sort [-bfnru] [file ...]` | Sort all input lines by unsigned byte order; `-b` ignores leading spaces/tabs, `-f` folds ASCII case, `-n` compares leading decimal numbers, `-r` reverses, `-u` emits one line per equal comparison key |
| `uniq [-cdui] [-f N] [-s N] [-w N] [input [output]]` | Collapse adjacent equal lines; `-c` prefixes counts (minimum width seven), `-d` keeps repeated groups, `-u` keeps single lines, `-i` folds ASCII case. Skip N blank-separated fields, then N bytes; compare at most N bytes with `-w` |
| `cut (-b LIST \| -c LIST \| -f LIST) [-d delimiter] [-s] [file ...]` | Select one-based byte/character positions or fields. Lists accept `N`, `N-M`, `N-`, `-M`, separated by commas. Fields default to tab; `-d` selects one byte (empty means NUL). `-s` suppresses undelimited lines |
| `tr [-cCdst] SET1 [SET2]` | Translate stdin bytes; `-d` deletes SET1, `-s` squeezes adjacent repeats from the last set after deletion/translation, `-c`/`-C` complements SET1 over all 256 bytes. `-t` limits translation to SET2 length |

Counts accept decimal integers; `-n2` and `-c32` also work for head/tail, and
more accepts `-n2`. The last head/tail count option selects the unit and count.
These are intentionally small Unix-style subsets: no size suffixes, negative
head counts, tail follow mode, locale/Unicode word handling, recursive grep,
extended regular expressions or multiple grep patterns. Long option aliases
other than `--help` are not implemented.

Sort buffers at most **128 KiB including line terminators and 4096 lines** across
all operands. Exceeding either bound fails before emitting sorted output. There
is no temporary-file/external sort. Numeric comparison is exact for decimal text
of any length within that bound: leading spaces/tabs, an optional minus sign,
digits and a decimal point. Missing numbers compare as zero; plus signs,
exponents, thousands separators and locale formats are not recognized. Equal
keys use the full original line as a bytewise tie-breaker, except with `-u`, which
retains the first input representative. There are no `-k` keys, merge/check modes
or `-o` output operand; use shell redirection to a separate file.

Uniq remembers only two lines and compares adjacent groups, so use `sort | uniq`
for global deduplication. It keeps each group's first original line. `-d -u`
together select no groups. Field skipping counts leading spaces/tabs plus the
following nonblank field; byte skipping and comparison width apply afterward.
Zero is valid for uniq counts, including `-w0` (all lines compare equal).
Cut emits selections in input order, once per position even for overlapping or
out-of-order ranges. Non-delimited lines pass through unchanged unless `-s` is
set; empty fields count. `-d`/`-s` require `-f`. `-c` selects bytes just like `-b`;
there is no multibyte decoding. Cut supports at most 128 ranges. Uniq and cut
accept **64 KiB per line**, excluding the newline; total input length is unbounded.
Sort, uniq and cut treat NUL/high bytes as data and terminate a final partial line
with a newline. Unsupported options, malformed ranges/sets and exceeded limits
are explicit failures. Usage errors exit 2; input/output/limit failures exit 1.

Tr preserves every byte not selected for translation/deletion and adds no newline.
Sets support literals, ascending byte ranges (`a-z`), backslash escapes
(`\\`, `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\v`), and one-to-three-digit
octal escapes through `\377`, including `\000`. Quote sets to preserve shell
metacharacters; double every backslash for HackOS shell parsing, even inside
quotes (for example, `tr -d "\\000"`). ASCII classes are `[:alnum:]`, `[:alpha:]`,
`[:blank:]`, `[:cntrl:]`, `[:digit:]`, `[:graph:]`, `[:lower:]`, `[:print:]`,
`[:punct:]`, `[:space:]`, `[:upper:]` and `[:xdigit:]`. Translation's SET2 may use
case classes only, paired with opposite case classes at the same position in
SET1. A short SET2 repeats its last byte unless `-t` is set; a duplicate SET1 byte
uses its last translation. `-d` takes one set, `-ds` two; `-s` alone takes one set
for squeezing, or two for translation followed by squeezing. Each expanded set
allows 1024 bytes; input size is unbounded. Equivalence/repetition expressions,
locale collation, Unicode and long option aliases other than `--help` are absent.

The grep pattern subset includes literals, `.`, leading `^`, trailing `$`, `*`,
bracket sets/ranges/negation (`[abc]`, `[a-z]`, `[^abc]`), and backslash-escaped
metacharacters. Quote patterns to protect them from shell parsing. Groups,
backreferences, interval expressions, POSIX character classes and collation
expressions are rejected. Matching uses a bounded state set with no recursive
backtracking. A pattern allows 128 atoms; an input line allows 64 KiB excluding
its newline. An oversized line or unsupported pattern is an explicit error.
NUL and high bytes are treated as data; there is no binary-file suppression.
Selected lines receive a terminating newline, including a final partial line.
Grep exits **0** when a line is selected, **1** for no matches and **2** for errors;
`-q` succeeds immediately on a match, even if an earlier operand failed.

Head/tail preserve exact bytes and final partial lines. Tail consumes the full
stream into a 128 KiB suffix ring; input length is unbounded, but an end-relative
suffix that does not fit fails explicitly. `tail -n +N` and `tail -c +N` stream forward without
that suffix limit. Wc and tee use fixed-size chunks. Filesystem writes retain
the system's 32 KiB file-size bound and exclusive writable handles; tee reports
those failures. Filters terminate on a broken pipe without noisy diagnostics.

More reads paging keys through `terminal_read`, independently of its stdin.
`terminal_info` identifies redirected stdout, which receives an exact byte copy
without prompts or keyboard reads. Background output to a terminal is rejected;
background redirection works. Terminal stdin is copied until Ctrl-D without
paging, because data and commands share the keyboard. On screen, tabs expand to
four-column stops, control bytes use caret notation, and high bytes display as
`?`; wrapped rows count toward a page. Redirected output is never sanitized.
A final full page at EOF needs no extra key. Early quit closes the input pipe so
upstream writers can finish with a broken-pipe result.

## Terminal appearance

```sh
terminal get                     # list all settings
terminal get font_size           # print 16 in a window; 30 in recovery console
terminal set font_size 60         # set a 60-pixel font height
terminal set bg_col 0x101820ff
terminal set font_col 0xe8eef2ff
terminal get rows | cat > /tmp/terminal-rows
terminal set font_size default
terminal reset                   # restore size and both colours
```

`terminal get [key]`, `terminal set <key> <value>`, `terminal reset` and
`terminal --help` are available to ordinary users. Queries work with pipes and
redirection and print no escape sequences. Set/reset requires the calling
process to own the foreground terminal, even for UID 0; background attempts
fail. Queries are readable without ownership and never consume keyboard input.

| Key | Meaning | Default (window / recovery console) |
| --- | --- | --- |
| `font_size` | Font height in pixels, even integers from 8 through 64; width is half the height | `16` / `30` |
| `bg_col` | Background as eight hexadecimal RGBA digits, optionally prefixed by `0x` or `0X` | `0x101b25ff` / `0x000000ff` |
| `font_col` | Text as eight hexadecimal RGBA digits | `0xe5eef5ff` / `0xffffffff` |
| `columns`, `rows` | Read-only count of complete character cells | Framebuffer dimensions divided by cell size |

Colour queries always print `0xrrggbbaa`. Alpha `ff` is opaque and `00` is
transparent: the background blends over black, then the text over that result.
Window surfaces still appear above the console. Each writable key accepts
`default`. Invalid keys, numbers or colours fail without changing settings.
`reset` can recover an invisible or low-contrast theme.

Every successful set/reset clears the visible terminal and its scrollback and
moves the cursor to the top left. It does not erase shell command history. Window
settings apply independently and last until reset/close; recovery-console settings
survive logout and last until reset/reboot. There is no saved per-account profile. Fonts use nearest-neighbour scaling of the bundled
PSF glyphs, so non-integral scaling can look less even. Window titles and controls
keep their native font. Pagers query the updated rows/columns when they start.
The custom `terminal` interface covers terminal appearance; it is not a POSIX
`stty` implementation and does not change terminal input modes.

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
| `network` | E1000 availability/link, leased/fallback addresses, DHCP state, resolved gateway MAC and bounded packet counters |
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

Process (`ps`), account (`users`, `groups`), directory (`ls -l`), and `sysinfo`
tables use space-padded columns that remain plain text when redirected. Names
are left aligned, numeric IDs/sizes right aligned, and hardware identifiers use
zero-padded hexadecimal. `ps` prints PID, PPID, PGID, UID, GID, state and name.

`sysinfo network` shows the experimental Ethernet/ARP state and is also included
in `sysinfo full`. It retains root-only authorization and supports redirection. It also reports
DHCP state, server, DNS address, lease duration/remaining time and T1/T2 intervals.
See [networking](networking.md) for a noVNC demo and counter definitions.

### ping

`ping [-c 1..10] <IPv4-address|hostname>` sends IPv4/ICMP Echo requests as any user. The default
is four probes with 32 data bytes and a three-second deadline each. Replies show
TTL and elapsed milliseconds at PIT resolution; the summary shows received
probes and loss. Exit status is 0 when every probe succeeds, 1 for loss,
unavailable networking, busy request slots or output errors, and 2 for invalid
arguments. Supports stdout redirection. Numeric targets use the current leased or fallback address and gateway; hostnames first use a five-second DNS lookup. ARP failure, destination unreachable
and time exceeded are reported separately from timeout. External ICMP depends
on the QEMU backend and host. See
[networking](networking.md) for the API and limits.

### nslookup

`nslookup <hostname>` resolves an ASCII hostname through the configured DNS server. It prints the normalized name, first IPv4 address and TTL, and supports
stdout redirection. Any user may run it. Exit status is 0 on success, 1 for an
invalid name, lookup/output failure, and 2 for an invalid argument count. DNS
errors, truncation and timeouts are reported separately. See
[networking](networking.md#udp-and-dns) for syntax and fixed limits.

### udp

`udp <IPv4-address|hostname> <port> <message>` sends a single UDP datagram and
waits for one reply. Messages may contain up to 512 bytes; quote spaces or an
empty message. Normal users can run it and redirect output. Received control or
binary bytes are displayed as `\xNN`. Exit status is 0 after a reply, 1 for
network/lookup/output failure, and 2 for invalid command arguments. Compose's
local echo demo is `udp 10.0.2.2 7777 "hello from HackOS"`. See
[networking](networking.md#connected-udp-endpoints) for bounded waits and semantics.

### TCP exchange demo

`tcp <IPv4-address|hostname> <port> <message>` sends at most 512 bytes, half-closes
and waits for up to 2048 response bytes and peer EOF. It reports clean completion,
byte counts and retransmissions; failure returns a nonzero exit status. Reply
control/binary bytes are escaped. Compose supplies a local echo peer:
`tcp 10.0.2.2 7778 "hello over TCP"`. It is available to normal users and supports
shell redirection. Exchanges time out after 20 seconds; this is not an interactive
stream socket utility. See [networking](networking.md#bounded-tcp-exchanges).

### Incremental TCP demo

`tcp --stream <host> <port> <count:1..1024> <message>` repeats a 1–512-byte message
on one connection, verifies each echo, and shuts down cleanly. Use the immediate
echo helper: `tcp --stream 10.0.2.2 7779 256 "hello over TCP"` transfers 3584 bytes
each way. Port 7778 remains the original helper that waits for EOF before
replying, so use it with the original `tcp host port message` command. Stream
mode waits with poll and reports byte totals; a mismatch, early EOF, socket
failure or timeout returns a nonzero exit status.
