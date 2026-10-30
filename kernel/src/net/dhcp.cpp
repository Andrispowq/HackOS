#include "dhcp.h"
#include "udp.h"
#include "route.h"
#include "ping.h"
#include "dns.h"
#include "socket.h"
#include "tcp.h"
#include "drivers/net/e1000.h"
#include "drivers/serial/serial.h"
#include "arch/x86_64/timer/pit.h"
#include "security/crypto.h"

namespace Net
{
namespace
{
const uint8_t zero[4] = {}, broadcast[4] = {255,255,255,255}, broadcastMac[6] = {255,255,255,255,255,255};
DhcpLease selected;
uint32_t state, xid, attempts;
uint64_t phaseStart, acquired, nextAttempt, lastWire;
bool up, requiresLease, randomReady, wired;

uint64_t Seconds(uint32_t seconds)
{
    return (uint64_t)seconds * TimerFrequency();
}

void Apply(const uint8_t* address, const uint8_t* mask, const uint8_t* gateway, const uint8_t* dns)
{
    uint64_t before = ConfigurationGeneration;
    ConfigureNetwork(address, mask, gateway, dns);
    if (before != ConfigurationGeneration)
    {
        InvalidateEcho();
        InvalidateResolve();
        InvalidateUdpSockets();
        InvalidateTcp();
    }
}

void Begin(unsigned value, unsigned delay = 0)
{
    state = value;
    attempts = 0;
    phaseStart = tick;
    nextAttempt = tick + Seconds(delay);
    uint8_t random[16];
    randomReady = RandomSalt(random);
    if (randomReady)
    {
        xid = ((uint32_t)random[0] << 24) | ((uint32_t)random[1] << 16) | ((uint32_t)random[2] << 8) | random[3];
    }
}

void LoseLease()
{
    Apply(zero, zero, zero, zero);
    selected = {};
    Begin(HACKOS_DHCP_SELECTING, 1);
}
}

void DhcpLink(bool link, const uint8_t*)
{
    up = link;
    Apply(zero, zero, zero, zero);
    ResetNeighbors();
    selected = {};
    attempts = 0;
    state = HACKOS_DHCP_DOWN;
    if (up)
    {
        Begin(HACKOS_DHCP_SELECTING);
        SerialText("NET: DHCP discovering\n");
    }
}

void PollDhcp(const uint8_t* mac)
{
    if (!up) return;
    uint64_t age = (tick - acquired) / TimerFrequency();
    if (state >= HACKOS_DHCP_BOUND && state <= HACKOS_DHCP_REBINDING)
    {
        if (age >= selected.lease)
        {
            SerialText("NET: DHCP lease expired\n");
            LoseLease();
        }
        else if (state != HACKOS_DHCP_REBINDING && age >= selected.rebind)
        {
            state = HACKOS_DHCP_REBINDING;
            attempts = 0;
            nextAttempt = tick;
            SerialText("NET: DHCP rebinding\n");
        }
        else if (state == HACKOS_DHCP_BOUND && age >= selected.renew)
        {
            Begin(HACKOS_DHCP_RENEWING);
            SerialText("NET: DHCP renewing\n");
        }
    }
    if (state == HACKOS_DHCP_FALLBACK && tick - phaseStart >= Seconds(60)) Begin(HACKOS_DHCP_SELECTING);
    if (state == HACKOS_DHCP_BOUND || state == HACKOS_DHCP_FALLBACK) return;
    if ((state == HACKOS_DHCP_SELECTING || state == HACKOS_DHCP_REQUESTING) && tick - phaseStart >= Seconds(28))
    {
        if (requiresLease)
        {
            Begin(HACKOS_DHCP_SELECTING, 4);
        }
        else
        {
            const uint8_t address[4] = {10,0,2,15}, mask[4] = {255,255,255,0};
            const uint8_t gateway[4] = {10,0,2,2}, dns[4] = {10,0,2,3};
            Apply(address, mask, gateway, dns);
            selected = {};
            state = HACKOS_DHCP_FALLBACK;
            phaseStart = tick;
            SerialText("NET: DHCP unavailable; static QEMU fallback\n");
        }
        return;
    }
    if (!randomReady || tick < nextAttempt || (wired && tick - lastWire < TimerTicksForMilliseconds(100))) return;
    uint8_t peerMac[6];
    const uint8_t* target = broadcast;
    const uint8_t* source = zero;
    if (state == HACKOS_DHCP_RENEWING || state == HACKOS_DHCP_REBINDING) source = LocalAddress;
    for (unsigned i = 0; i < 6; ++i)
    {
        peerMac[i] = broadcastMac[i];
    }
    if (state == HACKOS_DHCP_RENEWING)
    {
        uint8_t hop[4];
        target = selected.server;
        if (!NextHop(target, hop) || ResolveNeighbor(hop, peerMac) != NeighborState::Resolved) return;
    }
    uint8_t data[300], frame[UdpFrameMax];
    unsigned size = BuildDhcp(data, xid, mac, state, selected);
    uint64_t elapsed = (tick - phaseStart) / TimerFrequency();
    if (elapsed > 65535) elapsed = 65535;
    data[8] = elapsed >> 8;
    data[9] = elapsed;
    unsigned length = BuildUdp(frame, mac, peerMac, source, target, 68, 67, data, size);
    lastWire = tick;
    wired = true;
    nextAttempt = tick + TimerTicksForMilliseconds(100);
    if (Transmit(frame, length))
    {
        if (attempts != 0xffffffff) ++attempts;
        unsigned delay = attempts == 1 ? 4 : attempts == 2 ? 8 : 16;
        if (state == HACKOS_DHCP_RENEWING || state == HACKOS_DHCP_REBINDING)
        {
            uint32_t boundary = state == HACKOS_DHCP_RENEWING ? selected.rebind : selected.lease;
            delay = (boundary - age) / 2;
            if (!delay) delay = 1;
            if (delay > 60) delay = 60;
        }
        nextAttempt = tick + Seconds(delay);
    }
}

void ReceiveDhcp(const uint8_t* frame, unsigned size, const uint8_t* mac)
{
    if (!up || !randomReady || !attempts || state == HACKOS_DHCP_BOUND || state == HACKOS_DHCP_FALLBACK) return;
    UdpPacket packet;
    DhcpLease offer;
    if (!ParseUdp(frame, size, mac, LocalAddress, packet, true) || packet.sourcePort != 67 || packet.destinationPort != 68 ||
        !ParseDhcp(packet.data, packet.size, xid, mac, offer) || !EqualBytes(packet.source, offer.server, 4))
    {
        return;
    }
    if (state == HACKOS_DHCP_SELECTING)
    {
        if (offer.type != 2 || !ValidateLease(offer)) return;
        selected = offer;
        state = HACKOS_DHCP_REQUESTING;
        attempts = 0;
        phaseStart = nextAttempt = tick;
        return;
    }
    if (state != HACKOS_DHCP_REQUESTING && state != HACKOS_DHCP_RENEWING && state != HACKOS_DHCP_REBINDING) return;
    if (state != HACKOS_DHCP_REBINDING && !EqualBytes(offer.server, selected.server, 4)) return;
    if (offer.type == 6)
    {
        requiresLease = true;
        SerialText("NET: DHCP NAK; restarting\n");
        LoseLease();
        return;
    }
    if (offer.type != 5 || !EqualBytes(offer.address, selected.address, 4)) return;
    // ACK may omit unchanged mask/router/DNS, but must supply a fresh lease duration.
    for (unsigned i = 0; i < 4; ++i)
    {
        if (!(offer.fields & 1)) offer.mask[i] = selected.mask[i];
        if (!(offer.fields & 2)) offer.gateway[i] = selected.gateway[i];
        if (!(offer.fields & 4)) offer.dns[i] = selected.dns[i];
    }
    offer.fields |= 7;
    if (!ValidateLease(offer)) return;
    bool renewal = state == HACKOS_DHCP_RENEWING || state == HACKOS_DHCP_REBINDING;
    selected = offer;
    acquired = tick;
    state = HACKOS_DHCP_BOUND;
    requiresLease = true;
    Apply(offer.address, offer.mask, offer.gateway, offer.dns);
    SerialText(renewal ? "NET: DHCP lease renewed\n" : "NET: DHCP lease bound\n");
}

void SnapshotDhcp(HackosHardwareInfo& info)
{
    auto& network = info.network;
    network.dhcpState = state;
    network.dhcpAttempts = attempts;
    bool leased = state >= HACKOS_DHCP_BOUND && state <= HACKOS_DHCP_REBINDING;
    uint64_t age = (tick - acquired) / TimerFrequency();
    network.leaseSeconds = leased ? selected.lease : 0;
    network.remainingSeconds = leased && age < selected.lease ? selected.lease - age : 0;
    network.renewSeconds = leased ? selected.renew : 0;
    network.rebindSeconds = leased ? selected.rebind : 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        network.dns[i] = network.available ? DnsAddress[i] : 0;
        network.dhcpServer[i] = leased ? selected.server[i] : 0;
    }
}
}
