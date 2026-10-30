# Experimental Ethernet and ARP

[System index](index.md) · [Devices](devices.md) · [Testing](../development/testing.md)

The first networking milestone supports one QEMU Intel 82540EM E1000
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
`10.0.2.15` and resolves `10.0.2.2`. It sends at most five ARP requests, one second
apart after link-up; serial output reports initialization, MAC, link transitions,
gateway resolution or timeout. A later valid reply may still resolve the gateway.
No DHCP, IPv4 packet processing, ICMP, UDP, TCP, DNS or sockets are implemented.
Diagnostics use a new section of the existing root-only hardware query; there
is no new syscall number. There is no general routing table or ARP cache yet.

## Browser demo

Start `docker compose up --build bios` and open
[noVNC](http://localhost:6080/vnc.html?autoconnect=true&resize=scale).
Create the first administrator or log into an existing administrator account,
then run this in a desktop terminal (the recovery shell works too):

```text
sysinfo network
sysinfo network > /tmp/network.txt
cat /tmp/network.txt
```

Expect link `up`, static address `10.0.2.15`, gateway `10.0.2.2`, gateway ARP
`resolved`, gateway MAC `52:55:0a:00:02:02`, and nonzero TX/RX counts. This is a
real guest-driver exchange with QEMU's virtual gateway. The browser transports
keyboard input and framebuffer pixels over noVNC; Ethernet frames travel through
the separately emulated NIC and QEMU user-network backend. Neither transport
depends on the other. The displayed IPv4 address is currently used only in ARP.

The report is a copied 128-byte hardware record (80-byte network member), with
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
requests for the local IP receive replies. Gateway resolution requires a probe
and a reply addressed to our IP and MAC from the configured gateway IP. ARP
itself provides no peer authentication. Unknown protocols are discarded.

## Validation

```sh
python3 tests/network.py
python3 tools/build.py --firmware bios --output out/network
python3 tools/build.py --firmware uefi --output out/network
python3 tests/qemu_network.py --firmware bios --image out/network/bios/HackOS.img
python3 tests/qemu_network.py --firmware uefi --image out/network/uefi/HackOS.img
python3 tests/qemu_network_demo.py --firmware bios --image out/network/bios/HackOS.img
python3 tests/qemu_network_demo.py --firmware uefi --image out/network/uefi/HackOS.img
```

The host test compiles the real codec and checks truncated/malformed frames under
ASan and UBSan. `--sanitizers undefined` is an explicit local fallback when the
host ASan runtime is unavailable. QEMU tests use disposable disk snapshots and
copied OVMF VARS, check serial resolution and independently decode the captured
Ethernet request/reply. Separate boots test absent and unsupported NICs. A loopback-only scripted peer
injects malformed/unrelated replies, checks 40 ARP request/reply exchanges to
wrap both rings, and a silent peer checks the five-attempt timeout.
The demo test also verifies actual shell output, copied-record bounds, root
authorization and redirected reports, and saves a framebuffer screenshot.
Packet logs and pcaps are saved under `tests/out/network/<firmware>/`; demo logs
and screenshots are under `tests/out/network-demo-<firmware>/`.

Register definitions follow Intel's
[8254x software developer manual](https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf).
The emulator configuration follows [QEMU network options](https://www.qemu.org/docs/master/system/qemu-manpage.html).
