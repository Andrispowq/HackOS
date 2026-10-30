# Experimental networking: Ethernet, ARP and ping

[System index](index.md) · [Devices](devices.md) · [Testing](../development/testing.md)

Networking currently supports one QEMU Intel 82540EM E1000
(`8086:100e`). PCI discovery initializes the device before userspace page tables
are created. Other NICs are ignored; absence of a supported card does not stop
boot. This is an emulator bring-up driver, not a claim of physical-card support.

Both Docker Compose/noVNC services enable the card by default. For a direct
QEMU run, enable it explicitly (the direct runner defaults to no NIC):

```sh
python3 tools/run_qemu.py --firmware bios --image out/network/bios/HackOS.img \
  --network --snapshot --serial-log out/network/bios/serial.log
```

Use `--firmware uefi` and the corresponding UEFI image for the other boot path.
QEMU user networking supplies the test gateway. The guest uses static address
`10.0.2.15/24` and default gateway `10.0.2.2`. Numeric IPv4 targets on that subnet
are resolved directly with ARP; other supported targets use the gateway's MAC
while retaining the destination IP. DHCP, UDP, TCP, DNS, configurable routes and
sockets are not implemented. Diagnostics use the root-only hardware query;
process-owned Echo requests use syscall 97.

## Browser demo

Start `docker compose up --build bios` and open
[noVNC](http://localhost:6080/vnc.html?autoconnect=true&resize=scale).
Create the first administrator or log into an existing administrator account,
then run this in a desktop terminal (the recovery shell works too):

```text
ping 10.0.2.2
ping -c 2 10.0.2.2
sysinfo network
sysinfo network > /tmp/network.txt
cat /tmp/network.txt
```

Expect link `up`, static address `10.0.2.15`, gateway `10.0.2.2`, gateway ARP
`resolved`, gateway MAC `52:55:0a:00:02:02`, and nonzero TX/RX counts. This is a
real guest-driver exchange with QEMU's virtual gateway. The browser transports
keyboard input and framebuffer pixels over noVNC; Ethernet frames travel through
the separately emulated NIC and QEMU user-network backend. Neither transport
depends on the other. `ping` sends real IPv4/ICMP Echo requests and reports the
validated reply TTL, elapsed milliseconds and loss. Normal users may run ping;
`sysinfo network` remains root-only.

The report is a copied 128-byte hardware record (88-byte network member), with
no pointers and unchanged existing section numbers. Reading it does not send
packets or restart probing. Counters are cumulative since boot: TX counts queued
frames, RX counts complete error-free frames accepted for protocol inspection,
TX drops count unavailable descriptor slots/invalid sends, and RX drops count
rejected descriptors, not all hardware losses or unknown protocols. ARP reply
counts are queued replies to requests for our address. Polling and snapshots
prevent task preemption during updates, without blocking or yielding.

To run noVNC without a NIC, override the Compose service command:
`docker compose run --rm --service-ports bios python3 tools/container.py --firmware bios`.
Stop an existing service first to free port 6080. Normal Compose startup passes
`--network` to the container entry point, which forwards it to QEMU.

## Ownership and bounds

The driver accepts a 32-bit, 128 KiB-aligned MMIO BAR, maps it supervisor-only
with caching disabled, masks device interrupts and enables PCI bus mastering
only after initializing descriptor rings. Reset polling has a fixed iteration
limit. MAC discovery uses QEMU's initialized receive-address registers.

There are 16 legacy receive descriptors and 16 transmit descriptors, each with
a permanent 2048-byte buffer in kernel BSS. The identity-mapped kernel ELF owns
this storage for the entire boot; it is never reclaimed as process memory.
DMA addresses include the offset inside the physical page: two 2048-byte buffers
share one 4096-byte page, and `PhysicalAddress()` returns only the page base.
Descriptor layouts are asserted, rings are page aligned, and memory fences
order CPU buffer access against device ownership. A transmit slot is reused only
after hardware reports completion, with one slot kept empty to distinguish full
from empty. A full transmit ring drops the attempted send.

Idle polls at most 16 receive descriptors per iteration, without allocating,
sleeping, or waiting for device completion. Error-marked and multi-descriptor
frames are dropped. Ethernet frames are bounded to 1514 bytes excluding FCS;
VLANs and jumbo frames are unsupported. Hardware strips receive FCS and adds
transmit FCS. ARP frames are zero padded to 60 bytes.

The ARP decoder checks lengths before access, Ethernet/IPv4 types, address widths,
operation, destination and matching unicast Ethernet/ARP sender MACs. Only ARP
requests for the local IP receive replies. A 16-entry neighbor cache accepts
replies only after probing that IP, addressed to our IP and MAC. Unsolicited
replies cannot populate or replace resolved entries. ARP itself provides no
peer authentication. Unknown protocols are discarded.

Resolved neighbors expire after 60 seconds. Resolution has a 2.5-second deadline,
up to five probes spaced at least 500 ms apart, and a global ten-probe/second
limit with round-robin service. Failed entries back off for five seconds; a late
solicited reply may resolve them during that interval. Empty entries are preferred,
then the oldest non-pending entry; pending resolutions are never evicted. If all
entries are pending, a new request waits within its existing Echo deadline.
Link transitions clear the cache. The gateway is probed once on link-up and
resolved again on demand after expiry. Snapshots report the subnet mask and
resolved-neighbor count without initiating traffic.

## IPv4/ICMP and process ownership

`ping [-c 1..10] <IPv4-address>` defaults to four requests. Each carries 32 data bytes
and has a three-second deadline from submission, including any wait for ARP or
a TX slot. Requests are spaced by one second after each result. Timing uses PIT
ticks, so displayed milliseconds have timer resolution and include idle polling
latency; they are not a high-precision network benchmark. Addresses must be four decimal octets, without leading zeros, whitespace or
DNS names. Unsupported destinations include 0/8, loopback, link-local,
multicast/reserved space, our own address and the local subnet's network and
broadcast addresses. This is an Echo client, not an Echo responder.

The gateway is the reliable target for the default noVNC demo. External Echo
replies depend on the host and QEMU backend: [QEMU user networking](https://www.qemu.org/docs/master/system/devices/net.html)
has ICMP limitations. A silent external target does not prove the guest route
failed. The controlled-router test below verifies off-subnet traffic without
requiring public Internet ICMP access.

Packets use a 20-byte IPv4 header, TTL 64 and Don't Fragment. The decoder checks
Ethernet destination/type, IPv4 version/header length, exact total length,
nonzero TTL, protocol and checksum, and the ICMP type/code/checksum. IP options,
fragments (including reserved flag/MF/nonzero offset), other protocols and unsupported ICMP types are rejected. Only a pending, transmitted request can accept a reply;
source/destination addresses, identifier, sequence and all 32 payload bytes must
match. A 64-bit generation in the payload prevents stale replies from matching
a later request when the 16-bit wire sequence wraps. This is correlation, not
cryptographic peer authentication.

Destination-unreachable (type 3, codes 0–15) and time-exceeded (type 11, codes
0–1) errors are accepted after validating the outer IPv4/ICMP checksums and
quoted original IP header. The quote must match the local source, target,
identifier, sequence and original Echo checksum. Any quoted payload bytes must
also match; a minimal RFC 792 quote contains no payload, so it cannot verify the
full generation token. Unrelated errors leave the request pending.

Syscall 97 `echo(operation, HackosEcho*)` is available to ordinary users:

- `HACKOS_ECHO_START` copies a 24-byte record and uses its four address bytes.
  It returns 0, `HACKOS_ERROR` for invalid address/pointer, or `HACKOS_RETRY` for
  a pending request owned by this process or an exhausted eight-entry pool.
- `HACKOS_ECHO_QUERY` copies the caller's record: address, state, elapsedMs,
  sequence and TTL (reply) or ICMP error code (error). It returns `HACKOS_NOT_FOUND` without a retained request.
  An invalid output buffer does not consume the result.
- `HACKOS_ECHO_CANCEL` ignores the pointer and releases only the caller's entry.

The kernel stores no user pointers. Entries belong to PIDs, are not inherited,
and are released during process reclamation. Completed results expire after ten
seconds; a later START replaces the caller's completed result. States are
pending, reply, timeout, unavailable (no NIC/link down before transmission),
unreachable, time exceeded and next-hop ARP failure. The 24-byte record remains
unchanged in size; its final 32-bit field is a `ttl`/`errorCode` union. The global send rate
is capped at ten Echo requests per second, including cancel/restart loops.
No syscall blocks inside a preemption guard: the command sleeps between copied
status queries while idle performs bounded packet work.

## Validation

```sh
python3 tests/network.py
python3 tools/build.py --firmware bios --output out/network
python3 tools/build.py --firmware uefi --output out/network
python3 tests/qemu_network.py --firmware bios --image out/network/bios/HackOS.img
python3 tests/qemu_network.py --firmware uefi --image out/network/uefi/HackOS.img
python3 tests/qemu_network_demo.py --firmware bios --image out/network/bios/HackOS.img
python3 tests/qemu_network_demo.py --firmware uefi --image out/network/uefi/HackOS.img
python3 tests/qemu_routing.py --firmware bios --image out/network/bios/HackOS.img
python3 tests/qemu_routing.py --firmware uefi --image out/network/uefi/HackOS.img
```

The host test compiles the real codec and checks truncated/malformed frames under
ASan and UBSan, including checksum vectors, malformed IPv4/ICMP packets, stale
reply rejection, pool exhaustion, timeouts, cancellation and rate limiting. `--sanitizers undefined` is an explicit local fallback when the
host ASan runtime is unavailable. QEMU tests use disposable disk snapshots and
copied OVMF VARS, check serial resolution and independently decode the captured
Ethernet request/reply. Separate boots test absent and unsupported NICs. A loopback-only scripted peer
injects malformed/unrelated replies, checks 40 ARP request/reply exchanges to
wrap both rings, and a silent peer checks the five-attempt timeout.
The demo test also verifies actual shell output, copied-record bounds, root
authorization and redirected reports, ping from root and normal users, invalid
ABI pointers, child ownership/exit cleanup and no-NIC behavior. It independently
checks captured ICMP request/reply checksums and payloads, and saves framebuffer
screenshots. Distinct sequences catch accidental DMA aliases between ring slots.
Packet logs and pcaps are saved under `tests/out/network/<firmware>/`; demo logs
and screenshots are under `tests/out/network-demo-<firmware>/`.

`qemu_routing.py` supplies a loopback-only virtual router with distinct gateway
and neighbor MACs. It verifies direct and off-subnet Echo traffic, normal-user
access, matching and unrelated ICMP errors, silent targets, ARP failure, invalid
addresses and link-down/up cache recovery. Artifacts are saved in
`tests/out/routing-<firmware>/`. Host tests also check address parsing, route
selection, cache saturation, unsolicited replies, expiry and backoff.

Register definitions follow Intel's
[8254x software developer manual](https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf).
The emulator configuration follows [QEMU network options](https://www.qemu.org/docs/master/system/qemu-manpage.html).

IPv4 and ICMP wire formats follow [RFC 791](https://www.rfc-editor.org/rfc/rfc791.html)
and [RFC 792](https://www.rfc-editor.org/rfc/rfc792.html).
