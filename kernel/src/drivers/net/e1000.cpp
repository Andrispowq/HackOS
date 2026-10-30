#include "e1000.h"
#include "arch/x86_64/paging/paging.h"
#include "arch/x86_64/timer/pit.h"
#include "drivers/serial/serial.h"
#include "net/arp.h"
#include "net/ping.h"
#include "net/dns.h"
#include "net/socket.h"
#include "net/dhcp.h"
#include "net/route.h"
#include "arch/x86_64/interrupts/guard.h"

extern PageTableManager KernelDirectory;

namespace Net
{
namespace
{
constexpr uint32_t Count = 16;
constexpr uint32_t BufferSize = 2048;
constexpr uint32_t CTRL = 0x0000, STATUS = 0x0008, IMC = 0x00d8;
constexpr uint32_t RCTL = 0x0100, TCTL = 0x0400, TIPG = 0x0410;
constexpr uint32_t RDBAL = 0x2800, RDBAH = 0x2804, RDLEN = 0x2808, RDH = 0x2810, RDT = 0x2818;
constexpr uint32_t TDBAL = 0x3800, TDBAH = 0x3804, TDLEN = 0x3808, TDH = 0x3810, TDT = 0x3818;

struct RxDescriptor
{
    uint64_t address;
    uint16_t length, checksum;
    uint8_t status, errors;
    uint16_t special;
};

struct TxDescriptor
{
    uint64_t address;
    uint16_t length;
    uint8_t checksumOffset, command, status, checksumStart;
    uint16_t special;
};

static_assert(sizeof(RxDescriptor) == 16 && sizeof(TxDescriptor) == 16, "E1000 descriptor ABI");
// The kernel ELF is physically contiguous and identity mapped. These permanently
// reserved BSS buffers never become userspace pages or move while DMA is active.
alignas(4096) volatile RxDescriptor rx[Count];
alignas(4096) volatile TxDescriptor tx[Count];
alignas(4096) uint8_t receiveBuffers[Count][BufferSize];
alignas(4096) uint8_t transmitBuffers[Count][BufferSize];
volatile uint32_t* registers;
uint32_t nextRx, nextTx;
uint64_t observedConfiguration;
bool ready, announced, linkUp, dropping, gatewayPrimed;
uint8_t mac[6];
uint64_t txFrames, rxFrames, txDropped, rxDropped, arpReplies;
const uint8_t* localIp = LocalAddress;
const uint8_t* gatewayIp = GatewayAddress;

uint32_t Read(uint32_t offset)
{
    return registers[offset / 4];
}

void Write(uint32_t offset, uint32_t value)
{
    registers[offset / 4] = value;
    (void)Read(STATUS); // Flush posted MMIO writes.
}

void Fence()
{
    asm volatile("mfence" ::: "memory");
}

void PrintMac(const uint8_t* address)
{
    const char* hex = "0123456789abcdef";
    for (unsigned i = 0; i < 6; ++i)
    {
        if (i) SerialCharacter(':');
        SerialCharacter(hex[address[i] >> 4]);
        SerialCharacter(hex[address[i] & 15]);
    }
    SerialCharacter('\n');
}

bool Send(const uint8_t* frame, uint32_t length)
{
    if (length < 14 || length > 1514 || !(tx[nextTx].status & 1))
    {
        ++txDropped;
        return false;
    }
    uint32_t following = (nextTx + 1) % Count;
    // Keep one slot empty to distinguish full from empty.
    if (following == Read(TDH))
    {
        ++txDropped;
        return false;
    }
    Fence();
    for (uint32_t i = 0; i < length; ++i)
    {
        transmitBuffers[nextTx][i] = frame[i];
    }
    tx[nextTx].length = (uint16_t)length;
    tx[nextTx].command = 1 | 2 | 8; // EOP, insert FCS, report status.
    tx[nextTx].status = 0;
    Fence();
    nextTx = following;
    Write(TDT, nextTx);
    ++txFrames;
    return true;
}

void Receive(const uint8_t* frame, uint32_t length)
{
    ReceiveDhcp(frame, length, mac);
    if (!NetworkConfigured()) return;
    ReceiveEcho(frame, length, mac, localIp);
    ReceiveResolve(frame, length, mac, localIp);
    ReceiveUdpSockets(frame, length, mac, localIp);
    ArpPacket packet;
    if (!ParseArp(frame, length, mac, packet) || !EqualBytes(packet.targetIp, localIp, 4)) return;
    if (packet.operation == 1)
    {
        uint8_t reply[ArpFrameSize];
        BuildArp(reply, 2, mac, localIp, packet.senderMac, packet.senderIp);
        if (Send(reply, sizeof(reply)))
        {
            ++arpReplies;
        }
    }
    else
    {
        auto before = InspectNeighbor(gatewayIp).state;
        ReceiveNeighbor(frame, packet, mac);
        auto gateway = InspectNeighbor(gatewayIp);
        if (before != NeighborState::Resolved && gateway.state == NeighborState::Resolved)
        {
            SerialText("NET: ARP gateway ");
            PrintNetworkAddress(gatewayIp);
            SerialText(" resolved ");
            PrintMac(gateway.mac);
        }
    }
}
}

bool Transmit(const uint8_t* frame, uint32_t length)
{
    return ready && Send(frame, length);
}

void InitialiseE1000(PCI::PCIDeviceHeader* device)
{
    if (ready || device->VendorID != 0x8086 || device->DeviceID != 0x100e ||
        (device->HeaderType & 0x7f) != 0)
    {
        return;
    }
    auto* header = (volatile PCI::PCIHeaderType_0*)device;
    uint32_t bar = header->BAR0;
    // This first driver supports only the 82540EM's 32-bit 128 KiB MMIO BAR.
    if ((bar & 15) || !(bar & ~15U) || (bar & ~15U) > 0xfffe0000U || (bar & 0x1ffff))
    {
        SerialText("NET: unsupported E1000 BAR\n");
        return;
    }
    uint64_t base = bar;
    for (uint64_t offset = 0; offset < 0x20000; offset += 4096)
    {
        KernelDirectory.MapMemory(base + offset, base + offset, 3 | (uint64_t)PageTableFlags::CacheDisabled |
                                                                  (uint64_t)PageTableFlags::WriteThrough);
    }
    registers = (volatile uint32_t*)base;
    // Keep bus mastering disabled until every descriptor is initialized.
    header->Header.Command = (header->Header.Command | 2 | (1 << 10)) & ~4U;
    Write(IMC, 0xffffffff);
    Write(RCTL, 0);
    Write(TCTL, 0);
    Write(CTRL, Read(CTRL) | (1U << 26));
    uint32_t budget = 100000;
    while ((Read(CTRL) & (1U << 26)) && --budget)
    {
        asm volatile("pause");
    }
    if (!budget)
    {
        SerialText("NET: E1000 reset timeout\n");
        return;
    }
    Write(IMC, 0xffffffff);
    uint32_t low = Read(0x5400), high = Read(0x5404);
    for (unsigned i = 0; i < 4; ++i)
    {
        mac[i] = (uint8_t)(low >> (i * 8));
    }
    mac[4] = (uint8_t)high;
    mac[5] = (uint8_t)(high >> 8);
    const uint8_t zero[6] = {};
    if (!(high & (1U << 31)) || (mac[0] & 1) || EqualBytes(mac, zero, 6))
    {
        SerialText("NET: E1000 invalid receive MAC\n");
        return;
    }
    for (unsigned i = 0; i < 128; ++i)
    {
        Write(0x5200 + i * 4, 0); // Multicast table.
    }
    for (unsigned i = 1; i < 16; ++i)
    {
        Write(0x5404 + i * 8, 0); // Disable other receive addresses.
    }
    for (unsigned i = 0; i < Count; ++i)
    {
        // PhysicalAddress returns the containing frame base, not a byte address.
        // Two 2048-byte buffers share a page; preserve the second buffer's offset.
        uint64_t receiveAddress = (uint64_t)receiveBuffers[i];
        uint64_t transmitAddress = (uint64_t)transmitBuffers[i];
        rx[i].address = KernelDirectory.PhysicalAddress(receiveAddress) + (receiveAddress & 4095);
        tx[i].address = KernelDirectory.PhysicalAddress(transmitAddress) + (transmitAddress & 4095);
        tx[i].status = 1;
    }
    uint64_t receiveBase = KernelDirectory.PhysicalAddress((uint64_t)rx);
    uint64_t transmitBase = KernelDirectory.PhysicalAddress((uint64_t)tx);
    Write(RDBAL, (uint32_t)receiveBase);
    Write(RDBAH, (uint32_t)(receiveBase >> 32));
    Write(RDLEN, sizeof(rx));
    Write(RDH, 0);
    Write(RDT, Count - 1);
    Write(TDBAL, (uint32_t)transmitBase);
    Write(TDBAH, (uint32_t)(transmitBase >> 32));
    Write(TDLEN, sizeof(tx));
    Write(TDH, 0);
    Write(TDT, 0);
    Write(TIPG, 10 | (8 << 10) | (6 << 20));
    Fence();
    header->Header.Command |= 4;
    Write(CTRL, Read(CTRL) | (1 << 6)); // Set link up.
    Write(TCTL, 2 | 8 | (15 << 4) | (64 << 12));
    Write(RCTL, 2 | (1 << 15) | (1 << 26)); // Enable, broadcast, strip FCS; 2048-byte buffers.
    ready = true;
    SerialText("NET: E1000 ready MAC ");
    PrintMac(mac);
}

void Poll()
{
    // No blocking operations occur here. Keep copied diagnostics consistent
    // when a user query runs after a timer-driven task switch.
    PreemptionGuard guard;
    if (!ready)
    {
        if (!announced) SerialText("NET: no supported NIC\n");
        announced = true;
        PollEcho(false, false, mac, localIp);
        PollResolve(false, false, mac, localIp);
        PollUdpSockets(false, false, mac, localIp);
        return;
    }
    bool currentLink = (Read(STATUS) & 2) != 0;
    if (!announced || currentLink != linkUp)
    {
        SerialText(currentLink ? "NET: link up\n" : "NET: link down\n");
        announced = true;
        linkUp = currentLink;
        DhcpLink(linkUp, mac);
        gatewayPrimed = false;
    }
    PollDhcp(mac);
    // Never monopolize idle, even when the host continuously supplies frames.
    for (unsigned work = 0; work < Count; ++work)
    {
        uint8_t status = rx[nextRx].status;
        if (!(status & 1)) break;
        Fence();
        bool end = status & 2;
        uint16_t length = rx[nextRx].length;
        if (!dropping && end && !rx[nextRx].errors && length <= BufferSize)
        {
            ++rxFrames;
            Receive(receiveBuffers[nextRx], length);
        }
        else
        {
            ++rxDropped;
        }
        dropping = !end;
        rx[nextRx].status = 0;
        Fence();
        Write(RDT, nextRx);
        nextRx = (nextRx + 1) % Count;
    }
    if (observedConfiguration != ConfigurationGeneration)
    {
        observedConfiguration = ConfigurationGeneration;
        gatewayPrimed = false;
    }
    if (linkUp && NetworkConfigured() && !gatewayPrimed)
    {
        uint8_t gatewayMac[6];
        ResolveNeighbor(gatewayIp, gatewayMac);
        gatewayPrimed = true;
    }
    PollEcho(ready, linkUp && NetworkConfigured(), mac, localIp);
    PollResolve(ready, linkUp && NetworkConfigured(), mac, localIp);
    PollUdpSockets(ready, linkUp && NetworkConfigured(), mac, localIp);
    if (linkUp) PollNeighbors(mac);
}

void Snapshot(HackosHardwareInfo& info)
{
    PreemptionGuard guard;
    auto& network = info.network;
    network.available = ready;
    network.linkUp = linkUp;
    auto gateway = InspectNeighbor(gatewayIp);
    network.arpState = gateway.state == NeighborState::Resolved ? HACKOS_ARP_RESOLVED :
                       gateway.state == NeighborState::Failed ? HACKOS_ARP_TIMEOUT : HACKOS_ARP_WAITING;
    network.attempts = gateway.attempts;
    for (unsigned i = 0; i < 6; ++i)
    {
        network.mac[i] = mac[i];
        network.gatewayMac[i] = gateway.mac[i];
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        network.address[i] = ready ? localIp[i] : 0;
        network.gateway[i] = ready ? gatewayIp[i] : 0;
        network.netmask[i] = ready ? SubnetMask[i] : 0;
    }
    network.txFrames = txFrames;
    network.rxFrames = rxFrames;
    network.txDropped = txDropped;
    network.rxDropped = rxDropped;
    network.arpReplies = arpReplies;
    network.neighbors = NeighborCount();
    SnapshotDhcp(info);
}

}
