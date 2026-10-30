#include "net/dhcp.h"
#include "net/udp.h"
#include "net/icmp.h"
#include "net/route.h"
#include "net/ping.h"
#include "net/dns.h"
#include "net/socket.h"
#include "hackos/abi.h"
#include <cassert>
#include <initializer_list>

extern volatile uint64_t tick;
extern unsigned sends, transmittedLength;
extern uint8_t transmitted[1514];
static const uint8_t mac[6] = {0x52,0x54,0,0x12,0x34,0x56}, peer[6] = {0x52,0x55,192,0,2,130};
static const uint8_t address[4] = {192,0,2,129}, mask[4] = {255,255,255,128};
static const uint8_t gateway[4] = {192,0,2,130}, dns[4] = {192,0,2,131};
static const uint8_t broadcast[4] = {255,255,255,255}, broadcastMac[6] = {255,255,255,255,255,255};

static uint32_t Id()
{
    return ((uint32_t)transmitted[46] << 24) | ((uint32_t)transmitted[47] << 16) |
           ((uint32_t)transmitted[48] << 8) | transmitted[49];
}
static unsigned Reply(uint8_t* data, uint32_t id, uint8_t type)
{
    Net::DhcpLease lease = {};
    Net::BuildDhcp(data, id, mac, HACKOS_DHCP_SELECTING, lease);
    data[0] = 2;
    for (unsigned i = 0; i < 4; ++i)
    {
        data[16 + i] = address[i];
    }
    unsigned at = 240;
    auto bytes = [&](unsigned key, const uint8_t* value)
    {
        data[at++] = key;
        data[at++] = 4;
        for (unsigned i = 0; i < 4; ++i)
        {
            data[at++] = value[i];
        }
    };
    data[at++] = 53;
    data[at++] = 1;
    data[at++] = type;
    bytes(54, gateway);
    bytes(1, mask);
    bytes(3, gateway);
    bytes(6, dns);
    const uint8_t duration[4] = {0,0,0,16};
    bytes(51, duration);
    data[at++] = 255;
    return at;
}
static void Deliver(uint8_t* data, unsigned size)
{
    uint8_t frame[Net::UdpFrameMax];
    unsigned length = Net::BuildUdp(frame, peer, broadcastMac, gateway, broadcast, 67, 68, data, size);
    Net::ReceiveDhcp(frame, length, mac);
}
static uint32_t State()
{
    HackosHardwareInfo info = {};
    Net::SnapshotDhcp(info);
    return info.network.dhcpState;
}
static void Acquire()
{
    Net::PollDhcp(mac);
    uint32_t id = Id();
    uint8_t data[300];
    unsigned size = Reply(data, id, 2);
    Deliver(data, size);
    assert(State() == HACKOS_DHCP_REQUESTING);
    tick += 10;
    Net::PollDhcp(mac);
    assert(Id() == id);
    size = Reply(data, id, 5);
    Deliver(data, size);
    assert(State() == HACKOS_DHCP_BOUND);
}

void TestDhcp()
{
    uint8_t data[300];
    unsigned size = Reply(data, 123, 2);
    Net::DhcpLease lease;
    assert(Net::ParseDhcp(data, size, 123, mac, lease) && Net::ValidateLease(lease));
    assert(lease.renew == 8 && lease.rebind == 14);
    for (unsigned length = 0; length < size; ++length)
    {
        auto* shortened = new uint8_t[length];
        for (unsigned i = 0; i < length; ++i)
        {
            shortened[i] = data[i];
        }
        assert(!Net::ParseDhcp(shortened, length, 123, mac, lease));
        delete[] shortened;
    }
    for (unsigned offset : {0u,1u,2u,4u,24u,28u,236u,241u,244u})
    {
        data[offset] ^= 0x80;
        assert(!Net::ParseDhcp(data, size, 123, mac, lease));
        data[offset] ^= 0x80;
    }
    assert(Net::ParseDhcp(data, size, 123, mac, lease));
    lease.mask[3] = 129;
    assert(!Net::ValidateLease(lease));
    assert(Net::ParseDhcp(data, size, 123, mac, lease));
    lease.gateway[2] = 3;
    assert(!Net::ValidateLease(lease));
    assert(Net::ParseDhcp(data, size, 123, mac, lease));
    lease.fields |= 64;
    lease.renew = 16;
    assert(!Net::ValidateLease(lease));
    assert(Net::ParseDhcp(data, size, 123, mac, lease));
    lease.dns[3] = 255;
    assert(!Net::ValidateLease(lease));
    // DHCP can receive a 576-byte IPv4 message without enlarging public UDP payloads.
    uint8_t large[590] = {};
    unsigned base = Net::BuildUdp(large, peer, broadcastMac, gateway, broadcast, 67, 68, data, size);
    assert(base <= sizeof(large));
    large[16] = 2;
    large[17] = 64; // IP total 576.
    large[38] = 2;
    large[39] = 44; // UDP length 556, data 548.
    large[40] = large[41] = 0; // Permitted absent UDP checksum.
    large[24] = large[25] = 0;
    uint16_t checksum = Net::InternetChecksum(large + 14, 20);
    large[24] = checksum >> 8;
    large[25] = checksum;
    Net::UdpPacket largePacket;
    assert(Net::ParseUdp(large, sizeof(large), mac, address, largePacket, true) && largePacket.size == 548);
    assert(Net::ParseDhcp(largePacket.data, largePacket.size, 123, mac, lease));
    assert(!Net::ParseUdp(large, sizeof(large), mac, address, largePacket));
    // Startup silence reaches labelled static fallback, never an invented lease.
    tick += 10000;
    Net::DhcpLink(true, mac);
    assert(!Net::NetworkConfigured());
    Net::PollDhcp(mac);
    tick += 2800;
    Net::PollDhcp(mac);
    assert(State() == HACKOS_DHCP_FALLBACK && Net::LocalAddress[0] == 10);
    tick += 6000;
    Net::PollDhcp(mac);
    assert(State() == HACKOS_DHCP_SELECTING);
    // Link reset starts a new transaction; stale offer must not change state.
    uint32_t stale = Id();
    Net::DhcpLink(false, mac);
    assert(!Net::NetworkConfigured());
    tick += 100;
    Net::DhcpLink(true, mac);
    Net::PollDhcp(mac);
    size = Reply(data, stale, 2);
    Deliver(data, size);
    assert(State() == HACKOS_DHCP_SELECTING);
    Acquire();
    assert(Net::EqualBytes(Net::LocalAddress, address, 4) && Net::EqualBytes(Net::DnsAddress, dns, 4));
    uint8_t target[4] = {192,0,2,200}, hop[4];
    assert(Net::NextHop(target, hop) && Net::EqualBytes(hop, target, 4));
    target[3] = 1;
    assert(Net::NextHop(target, hop) && Net::EqualBytes(hop, gateway, 4));
    target[3] = 128;
    assert(!Net::NextHop(target, hop));
    target[3] = 255;
    assert(!Net::NextHop(target, hop));
    // Renewal needs the selected server's next-hop MAC.
    uint8_t hardware[6], arp[60];
    Net::ResolveNeighbor(gateway, hardware);
    tick += 10;
    Net::PollNeighbors(mac);
    Net::BuildArp(arp, 2, peer, gateway, mac, address);
    Net::ArpPacket packet;
    assert(Net::ParseArp(arp, sizeof(arp), mac, packet));
    Net::ReceiveNeighbor(arp, packet, mac);
    tick += 800;
    Net::PollDhcp(mac);
    assert(State() == HACKOS_DHCP_RENEWING);
    assert(Net::EqualBytes(transmitted, peer, 6) && Net::EqualBytes(transmitted + 26, address, 4));
    assert(Net::EqualBytes(transmitted + 54, address, 4)); // ciaddr
    size = Reply(data, Id(), 5);
    uint64_t generation = Net::ConfigurationGeneration;
    Deliver(data, size);
    assert(State() == HACKOS_DHCP_BOUND && generation == Net::ConfigurationGeneration);
    // Silence goes through rebinding, then expires and invalidates outstanding work.
    HackosEcho echo = {{192,0,2,200},0,0,0,{0}};
    assert(Net::EchoControl(990, HACKOS_ECHO_START, echo) == 0);
    HackosDatagram udp = {};
    udp.address[0] = 192;
    udp.address[2] = 2;
    udp.address[3] = 200;
    udp.peerPort = 7777;
    uint64_t handle = Net::UdpControl(990, HACKOS_UDP_OPEN, 0, udp);
    assert(!(handle >> 63));
    tick += 800;
    Net::PollDhcp(mac);
    tick += 600;
    Net::PollDhcp(mac);
    assert(State() == HACKOS_DHCP_REBINDING && Net::EqualBytes(transmitted, broadcastMac, 6));
    // Start fresh pending work just before expiry.
    Net::CancelEcho(990);
    assert(Net::EchoControl(990, HACKOS_ECHO_START, echo) == 0);
    assert(Net::UdpControl(990, HACKOS_UDP_SEND, handle, udp) == 0);
    HackosResolve lookup = {};
    const char* hostname = "demo.test";
    for (unsigned i = 0; hostname[i]; ++i)
    {
        lookup.name[i] = hostname[i];
    }
    assert(Net::ResolveControl(990, HACKOS_RESOLVE_START, lookup) == 0);
    assert(Net::UdpControl(990, HACKOS_UDP_STATUS, handle, udp) == 0);
    uint8_t received[Net::UdpFrameMax], payload[1] = {42};
    const uint8_t udpPeer[4] = {192,0,2,200};
    unsigned receivedSize = Net::BuildUdp(received, peer, mac, udpPeer, address, 7777, udp.localPort, payload, 1);
    Net::ReceiveUdpSockets(received, receivedSize, mac, address);
    assert(Net::UdpControl(990, HACKOS_UDP_STATUS, handle, udp) == 0 && udp.queued == 1);
    tick += 200;
    Net::PollDhcp(mac);
    assert(!Net::NetworkConfigured() && State() == HACKOS_DHCP_SELECTING);
    assert(Net::EchoControl(990, HACKOS_ECHO_QUERY, echo) == 0 && echo.state == HACKOS_ECHO_UNAVAILABLE);
    assert(Net::UdpControl(990, HACKOS_UDP_STATUS, handle, udp) == 0 && udp.state == HACKOS_UDP_UNAVAILABLE && udp.queued == 0);
    assert(Net::ResolveControl(990, HACKOS_RESOLVE_QUERY, lookup) == 0 && lookup.state == HACKOS_RESOLVE_UNAVAILABLE);
    Net::CancelResolve(990);
    Net::CancelEcho(990);
    Net::CloseUdpSockets(990);
    tick += 100;
    Acquire();
    tick += 800;
    Net::PollDhcp(mac);
    // Resolve again after the configuration changed.
    Net::PollNeighbors(mac);
    Net::BuildArp(arp, 2, peer, gateway, mac, address);
    assert(Net::ParseArp(arp, sizeof(arp), mac, packet));
    Net::ReceiveNeighbor(arp, packet, mac);
    Net::PollDhcp(mac);
    size = Reply(data, Id(), 6);
    Deliver(data, size);
    assert(!Net::NetworkConfigured() && State() == HACKOS_DHCP_SELECTING);
    tick += 2800;
    Net::PollDhcp(mac);
    assert(!Net::NetworkConfigured()); // Never fall back after losing a real lease.
    Net::DhcpLink(false, mac);
    const uint8_t original[4] = {10,0,2,15}, originalMask[4] = {255,255,255,0};
    const uint8_t originalGateway[4] = {10,0,2,2}, originalDns[4] = {10,0,2,3};
    Net::ConfigureNetwork(original, originalMask, originalGateway, originalDns);
}
