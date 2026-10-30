# Experimental networking: Ethernet, routing, DHCP, UDP and TCP

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
QEMU user networking supplies a DHCP server. The guest requests its IPv4 address,
subnet mask, default gateway and DNS server automatically. The normal lease is
`10.0.2.15/24`, gateway `10.0.2.2`, DNS `10.0.2.3`, but those are no longer routing
constants. Same-subnet targets resolve directly with ARP; other supported targets
use the supplied gateway while retaining the destination IP. Connected UDP and
bounded DNS lookup and bounded active TCP exchanges are available. General stream
sockets and listening TCP servers are not implemented.
Diagnostics use the root-only hardware query; Echo uses syscall 97, DNS lookup
uses syscall 98, connected UDP uses syscall 99 and TCP exchanges use syscall 100.

## Browser demo

Start `docker compose up --build bios` and open
[noVNC](http://localhost:6080/vnc.html?autoconnect=true&resize=scale).
Create the first administrator or log into an existing administrator account,
then run this in a desktop terminal (the recovery shell works too):

```text
ping 10.0.2.2
ping -c 2 10.0.2.2
nslookup example.com
ping -c 2 example.com
udp 10.0.2.2 7777 "hello from HackOS"
tcp 10.0.2.2 7778 "hello over TCP"
sysinfo network
sysinfo network > /tmp/network.txt
cat /tmp/network.txt
```

Expect link `up`, DHCP state `bound`, address `10.0.2.15`, gateway `10.0.2.2`, gateway ARP
`resolved`, gateway MAC `52:55:0a:00:02:02`, and nonzero TX/RX counts. This is a
real guest-driver exchange with QEMU's virtual gateway. The browser transports
keyboard input and framebuffer pixels over noVNC; Ethernet frames travel through
the separately emulated NIC and QEMU user-network backend. Neither transport
depends on the other. `ping` sends real IPv4/ICMP Echo requests and reports the
validated reply TTL, elapsed milliseconds and loss. Normal users may run ping;
`sysinfo network` remains root-only.

The report is a copied 128-byte hardware record (120-byte network member), with
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

`ping [-c 1..10] <IPv4-address|hostname>` defaults to four requests. Each carries 32 data bytes
and has a three-second deadline from submission, including any wait for ARP or
a TX slot. Requests are spaced by one second after each result. Timing uses PIT
ticks, so displayed milliseconds have timer resolution and include idle polling
latency; they are not a high-precision network benchmark. Numeric addresses must be four decimal octets, without leading zeros or
whitespace. Hostnames are resolved first through the DNS API below; the DNS
deadline is separate from each Echo deadline. Unsupported destinations include 0/8, loopback, link-local,
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

## UDP and DNS

`nslookup <hostname>` prints the normalized name, first IPv4 answer and TTL.
`sysinfo network` reports the configured DNS server. `ping <hostname>` resolves once, prints the selected address, then uses the
existing Echo path. A successful DNS lookup does not imply the target answers
ICMP. Both commands support normal users and stdout redirection.

The internal UDP codec supports up to 512 payload bytes, with a 20-byte IPv4
header and no fragments/options. Userspace endpoints carry UDP payloads only;
there is no raw Ethernet/IP interface. It emits
UDP pseudo-header checksums and checks incoming nonzero checksums; IPv4's
permitted zero UDP checksum is accepted. Ethernet padding is excluded using the
IP/UDP lengths. DNS accepts only the configured DNS server on port 53, matching
local port, transaction ID and question. IDs and ephemeral ports (49152–65535)
come from the existing bounded RDRAND source. Failure to obtain entropy rejects
START; live-port collisions return RETRY. Correlation is not DNS authentication.

Syscall 98 `resolve(operation, HackosResolve*)` uses a copied 272-byte record:
256-byte NUL-terminated name, 32-bit state, four address bytes, 32-bit TTL and
32-bit DNS error code. START reads only the name; QUERY copies the complete
record; CANCEL ignores the pointer. One lookup belongs to each PID, eight
lookups globally, with no inherited requests or retained user pointers. Exit
reclamation cancels entries. Completed results expire after ten seconds and a
new START replaces the caller's completed result. Invalid pointers/names/operations
return ERROR, a pending lookup or full pool returns RETRY, and a query without
an entry returns NOT_FOUND. Invalid output buffers do not consume results.

Each lookup has a five-second deadline including ARP, up to three transmissions
spaced at least one second apart, and a global ten-DNS-packet/second ceiling
preserved across cancellation. Idle services requests round-robin without
allocating, waiting or yielding under its preemption guard. Terminal states are
reply, timeout, unavailable, ARP failure, NXDOMAIN, server error, truncated and
no supported IPv4 answer. Link-down cancels pending lookups with unavailable.

Hostnames use ASCII letters, digits and hyphens with nonempty labels of at most
63 bytes, at most 253 bytes overall, no leading/trailing label hyphens, and at
least one letter. Case is normalized; one final dot is accepted. Numeric-looking
invalid addresses cannot silently become DNS queries. Queries request IN/A with
recursion desired. The decoder bounds names, backwards compression pointers
(16 jumps), records (32 total) and CNAME chains (eight aliases). It validates the
complete successful response before accepting an A record whose owner matches
the question or its CNAME chain; unrelated answers are ignored. The returned TTL
is the minimum along that chain. This is an uncached stub resolver: CNAME targets
must have an A answer in the same response. There is no IPv6/AAAA, EDNS, DNSSEC,
search suffix expansion, separate CNAME follow-up or TCP fallback. Truncated
responses produce an explicit error instead of accepting partial data.

The wire formats follow [RFC 768](https://www.rfc-editor.org/rfc/rfc768) and
[RFC 1035](https://www.rfc-editor.org/rfc/rfc1035).

## DHCP configuration and leases

Link-up starts DISCOVER → OFFER → REQUEST → ACK with a random transaction ID and
the NIC MAC/client identifier. The interface has no usable address until ACK or
startup fallback. Acquisition sends at 0, 4 and 12 seconds, with a 28-second
per-phase deadline. Replies must match the transaction, client MAC, UDP ports
67→68, server identifier and source IPv4 address. A selected server's OFFER is
not applied until its ACK arrives. Broadcast reception is enabled only in the
DHCP parser; ordinary UDP destinations and privileged local ports remain bounded.

The decoder checks BOOTP fields/cookie, lengths, critical-option duplicates,
end markers, contiguous masks and usable addresses. It selects the first router
and first DNS address from their option lists. Address, mask, router, DNS and a
lease of at least eight seconds are required; unchanged mask/router/DNS options
may be inherited from the selected lease when ACK omits them. The gateway must
be another usable address on the leased subnet. Masks /1 through /30 are
supported. T1/T2 default to half and seven-eighths of the lease, or use valid
server-supplied values with `0 < T1 < T2 < lease`.

At T1 the client unicasts REQUEST to the lease server, resolving its next-hop MAC
through ARP. At T2 it broadcasts REQUEST and may accept a different server's ACK
for the same leased address. Retries use half the remaining phase time, clamped
to 1–60 seconds for the short leases used by this emulator. A separate ten-packet
per-second limiter survives link changes. An ACK restarts the lease clock; the
address remains usable while renewal/rebinding is pending. Expiry, NAK or
link-down removes the address immediately. New discovery then begins with a
fresh transaction ID; old replies cannot restore the expired configuration.

If no valid lease has ever been acquired or NAK received during this boot,
startup acquisition failure enables the old static QEMU values after 28 seconds.
`sysinfo network` labels this **static fallback**, with no lease duration or DHCP
server. Discovery is retried after 60 seconds while this fallback remains usable.
Fallback is an emulator compatibility path, not a safe general-LAN configuration
policy. After a real lease or valid NAK, failure leaves networking unconfigured
until DHCP succeeds; it does not silently reuse the previous lease or defaults.

Configuration changes atomically replace the address/mask/gateway/DNS records,
clear ARP neighbors and fail pending Echo, DNS and UDP sends as unavailable.
UDP receive queues are cleared, while process-owned handles/port bindings remain
open and can send again after configuration succeeds. A renewal with identical
configuration preserves active traffic. Lease durations/remaining seconds,
renew/rebind intervals, server, DNS and phase attempts extend the network member
to 120 bytes; the outer copied hardware record remains 128 bytes and existing
field offsets are unchanged. Reading diagnostics never sends packets.

This is a bounded emulator DHCPv4 client, not a full DHCP implementation: it uses
one interface and one router/DNS address, accepts direct servers only (no relay
`giaddr`), and rejects overloaded/concatenated critical options. Incoming UDP
payloads are bounded to 548 bytes (576-byte IPv4 message), with no fragmentation
or IP options. There is no ARP duplicate-address detection/DECLINE, RELEASE,
persisted lease/INIT-REBOOT, manual configuration interface, DHCPv6 or DHCP
authentication yet. The protocol follows [RFC 2131](https://www.rfc-editor.org/rfc/rfc2131)
and [RFC 2132](https://www.rfc-editor.org/rfc/rfc2132) within these limits.

## Connected UDP endpoints

`udp <IPv4-address|hostname> <port> <message>` sends one datagram and displays
one reply, escaping binary/control bytes as `\xNN`. Quote messages containing
spaces. Empty messages and payloads up to 512 bytes are supported. Name lookup
uses the existing resolver. Exit status is 0 for a received reply and successful
output, 1 for a network/lookup/output failure, and 2 for invalid command arguments.
The command polls with scheduler sleeps: up to 200 send-status polls and 150
receive polls at 20 ms intervals. The kernel enforces a separate three-second
send deadline. A receive timeout does not imply that the peer failed to receive
or act on the sent message. UDP provides no delivery, ordering or duplicate
suppression guarantees.

Compose starts a development-only echo helper bound to `127.0.0.1:7777` inside
the container, reachable from the guest as `10.0.2.2:7777`. It returns exactly the
received bytes, including empty datagrams; oversized messages are discarded.
No Docker UDP port is published. For a direct run, add `--network --udp-demo`
to `tools/run_qemu.py`; the helper runs on the host loopback interface instead.
It is stopped with QEMU. Omit `--udp-demo` when supplying your own peer.

Syscall 99 `udp(operation, handle, HackosDatagram*)` is available to normal users:

- OPEN copies the peer IPv4 address, nonzero peer port and optional local port.
  The handle argument must be zero. Success returns a new positive handle;
  existing handles are never reused. Local ports 1024–49151 can be requested;
  zero selects a free port in 32768–49151. Bindings are exclusive, including
  across processes. Ports 49152–65535 remain reserved for the internal resolver.
- SEND copies `size` and up to 512 payload bytes to the endpoint's single pending
  transmit slot. It returns zero when queued, RETRY while that slot is pending,
  or ERROR for an invalid size/pointer. Peer fields in this record cannot retarget
  the endpoint. A zero-byte datagram is valid.
- STATUS copies peer/local ports, last send state, queued receive count, dropped
  receive count and successfully queued NIC transmission count. Its payload is
  zero. Send states are idle, pending, sent, timeout, unavailable and ARP failure.
  SENT means queued to the NIC, not acknowledged by the peer.
- RECEIVE copies one complete datagram from the oldest receive slot. An empty
  queue returns RETRY. A bad output pointer leaves the datagram queued: dequeue
  occurs only after successful copying, under the same dispatcher guard. The
  returned queue count is the count before dequeue. Unused payload bytes are zero.
- CLOSE ignores the pointer and releases the endpoint and all queued data.

The fixed record is 544 bytes: address[4], two 16-bit ports, four 32-bit fields
(size/state/queued/dropped), 64-bit sent count, and data[512] at offset 32.
Input/output buffers are copied in full; no user pointers are retained. A foreign,
closed or unknown handle returns NOT_FOUND. Invalid operations on a valid handle
return ERROR. OPEN returns RETRY for per-process/global exhaustion or an occupied
port. Reserved/invalid local ports and unsupported destination addresses return
ERROR. Sockets belong to PIDs, are not inherited and are reclaimed on exit.

There are sixteen endpoints globally, at most four per process. Each owns one
512-byte send slot and a four-datagram receive FIFO in permanent bounded kernel
storage. A full receive FIFO drops the newest datagram and increments a saturating
counter. Only packets from the configured peer address and port, sent to that
endpoint's local port, enter its FIFO. DNS ports cannot collide with endpoints.
UDP has no generation field on the wire: reopening a local port can receive a
late datagram from the same peer, even though stale handles remain invalid.

Idle services sends round-robin at up to twenty socket datagrams per second,
independently of the existing DNS/ARP/Echo ceilings. Closing/reopening cannot
reset the global limiter. Sends wait for the current routing/ARP within their deadline;
there is no retransmission after NIC submission. Link-down fails pending sends,
while received data and open bindings remain until consumed/closed. There is no
wildcard listening, socket inheritance, stream-descriptor/poll integration,
broadcast/multicast/loopback, fragmentation, or ICMP error delivery to UDP sockets
yet. These are connected datagram endpoints, not POSIX sockets.

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
python3 tests/qemu_dns.py --firmware bios --image out/network/bios/HackOS.img
python3 tests/qemu_dns.py --firmware uefi --image out/network/uefi/HackOS.img
python3 tests/qemu_udp.py --firmware bios --image out/network/bios/HackOS.img
python3 tests/qemu_udp.py --firmware uefi --image out/network/uefi/HackOS.img
python3 tests/qemu_dhcp.py --firmware bios --image out/network/bios/HackOS.img
python3 tests/qemu_dhcp.py --firmware uefi --image out/network/uefi/HackOS.img
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

`qemu_dns.py` uses a loopback-only resolver to validate UDP checksums, compressed
answers/CNAMEs, wrong-source/port/ID rejection, retries, DNS errors, timeouts,
normal-user hostname ping, copied ABI bounds, child ownership/exit cleanup and
link recovery. `--internet` explicitly opts into an `example.com` lookup through
QEMU user networking; CI does not depend on external DNS. Artifacts are under
`tests/out/dns-<firmware>/` (or `dns-<firmware>-internet/`).

`qemu_udp.py` validates endpoint ownership/exit cleanup, invalid pointers and
non-consuming failed receives, 0/512-byte payloads, FIFO order/overflow, peer
filtering, direct/routed/DNS destinations, redirection, ARP failure and no-NIC/link
recovery. `--host-echo` uses a real loopback UDP server through QEMU user networking.
Both modes need no external Internet access. Artifacts are in
`tests/out/udp-<firmware>/` and `tests/out/udp-<firmware>-host/`.

`qemu_dhcp.py` supplies a non-default /25 lease and checks the actual source
address, direct/routed destination MACs, configured DNS, UDP, renewal, broadcast
rebinding, expiry, NAK, malformed/stale offers and link recovery on both firmware
paths. Shared virtual peers provide DHCP to the older routing/DNS/UDP suites.
Artifacts are saved under `tests/out/dhcp-<firmware>/`. Host sanitizer tests also
cover startup fallback, prefix routing, malformed options and outstanding-request
invalidation at lease loss.

## Bounded TCP exchanges

`tcp <IPv4-address|hostname> <port> <message>` performs an active IPv4 TCP open,
sends 0–512 bytes, half-closes its sending direction after acknowledgement, and
collects up to 2048 response bytes through peer EOF. It succeeds only after its
FIN is acknowledged, the peer FIN is received, and the final ACK is queued to the
NIC. Binary/control reply bytes are escaped for terminal safety. Normal users
can run it; output supports redirection. Compose starts a loopback-only TCP echo
helper on port 7778, alongside the UDP helper. No extra host port is published.
The echo helper waits for the client's half-close before replying, demonstrating
that receiving remains possible after sending EOF. Direct runs can select
`--network --tcp-demo`.

Syscall 100 `tcp(operation, HackosTcp*)` uses a copied 2072-byte record:
address/peer port, kernel-selected local port, request size, result state,
received count, retransmission count and 2048 data bytes. START copies the first
`size` bytes (maximum 512) as the request. QUERY is nondestructive and returns
only initialized response bytes; CANCEL ignores its pointer. One result/request
belongs to each process, with eight owned slots globally. A process must cancel
its previous result before starting another exchange. Invalid inputs return
ERROR, missing ownership returns NOT_FOUND, and admission pressure returns RETRY.
Process exit cancels its exchange; no network operation blocks inside a guard.
The command sleeps between status queries. Results expire after ten seconds.

The implementation has 32 fixed records, including detached TIME-WAIT tuples.
After a clean close the tuple remains reserved for 120 seconds and answers
repeated FINs even after its process exits. Duplicate FIN restarts that timer.
Ephemeral ports 49152–65535 are selected without colliding with retained TCP
records. Admission is limited to ten starts per second; transmission is globally
limited to 100 segments per second, with rotating slot priority. The initial
sequence number comes from RDRAND-backed random bytes. Network configuration
loss/change fails pending exchanges and prevents old-tuple traffic from being
sent using the replacement configuration.

IPv4 and TCP lengths, checksums, destination and options are validated before
matching the connection tuple. Only the expected SYN acknowledgement opens the
connection. SYN advertises MSS 512; outgoing data respects the peer's MSS and
advertised window. At most one data segment is outstanding. Cumulative and
partial ACKs advance only over transmitted bytes; retransmission resends the
unacknowledged suffix. Sequence comparisons handle 32-bit wrap. Out-of-order
segments trigger a cumulative ACK without being buffered, and retransmitted
prefixes are trimmed. An established RST must match the next expected sequence;
a SYN-stage RST must acknowledge the SYN. SYN, data and FIN retransmissions use
1, 2, 4 and then 8 second intervals. Every exchange has a 20-second total deadline.
Errors distinguish timeout, unavailable network, failed ARP, reset/refusal and
response overflow. Partial data on a failed exchange is not reported as success.

This is an experimental bounded transaction client, not a complete general TCP
stack. It has no listen/accept, simultaneous open, stream descriptors, poll
integration, out-of-order queue, SACK, window scaling, timestamps, urgent-data
interface, adaptive RTT/RTO or zero-window persist probes. A peer that keeps its
window closed or never sends EOF times out. The conservative one-segment flight
limit replaces a growing congestion window in this milestone. There is no IP
fragmentation/reassembly or ICMP/PMTU feedback. DNS TCP fallback, HTTP and TLS are
still future work. Wire/state handling is based on
[RFC 9293](https://www.rfc-editor.org/rfc/rfc9293.html); these explicit limits are
not a claim of full RFC conformance.

`tests/qemu_tcp.py` exercises both firmware paths with a deterministic virtual
peer: SYN/data/FIN loss, duplicate/overlapping and out-of-order data, sequence
wrap, forged ACK/RST, binary/max-sized input, empty exchanges, copied-pointer
bounds, ownership, process cleanup, direct/routed/DNS peers, redirection and link
recovery. `--host-echo` runs against a real host TCP socket through QEMU user
networking. Host sanitizer tests also cover malformed/truncated packets, partial
ACKs, TIME-WAIT cleanup, zero-window updates, buffer exhaustion and slot limits.
