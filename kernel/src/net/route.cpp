#include "route.h"
#include "drivers/net/e1000.h"
#include "drivers/serial/serial.h"
#include "arch/x86_64/timer/pit.h"

namespace Net
{
const uint8_t LocalAddress[4] = {10, 0, 2, 15};
const uint8_t SubnetMask[4] = {255, 255, 255, 0};
const uint8_t GatewayAddress[4] = {10, 0, 2, 2};
namespace
{
struct Neighbor
{
    uint8_t address[4], mac[6];
    NeighborState state;
    unsigned attempts;
    bool timeoutLogged;
    uint64_t created, updated, lastAttempt;
};
Neighbor neighbors[16];
unsigned cursor;
uint64_t lastProbe;
bool probed;

bool OnLink(const uint8_t* address)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        if ((address[i] & SubnetMask[i]) != (LocalAddress[i] & SubnetMask[i])) return false;
    }
    return true;
}

void Age(Neighbor& neighbor)
{
    uint64_t now = tick;
    if (neighbor.state == NeighborState::Pending && now - neighbor.created >= TimerTicksForMilliseconds(2500))
    {
        neighbor.state = NeighborState::Failed;
        neighbor.updated = now;
    }
    if ((neighbor.state == NeighborState::Resolved && now - neighbor.updated >= TimerTicksForMilliseconds(60000)) ||
        (neighbor.state == NeighborState::Failed && now - neighbor.updated >= TimerTicksForMilliseconds(5000)))
    {
        neighbor = {};
    }
}

Neighbor* Find(const uint8_t* address)
{
    for (auto& neighbor : neighbors)
    {
        Age(neighbor);
        if (neighbor.state != NeighborState::Missing && EqualBytes(neighbor.address, address, 4)) return &neighbor;
    }
    return nullptr;
}
}

bool NextHop(const uint8_t* destination, uint8_t* hop)
{
    // No loopback, link-local, multicast, reserved, self or local broadcast delivery yet.
    if (!destination[0] || destination[0] == 127 || destination[0] >= 224 ||
        (destination[0] == 169 && destination[1] == 254) || EqualBytes(destination, LocalAddress, 4))
    {
        return false;
    }
    bool local = OnLink(destination);
    if (local && (destination[3] == 0 || destination[3] == 255)) return false;
    for (unsigned i = 0; i < 4; ++i)
    {
        hop[i] = local ? destination[i] : GatewayAddress[i];
    }
    return true;
}

NeighborInfo InspectNeighbor(const uint8_t* address)
{
    NeighborInfo result = {};
    if (auto* neighbor = Find(address))
    {
        result.state = neighbor->state;
        result.attempts = neighbor->attempts;
        for (unsigned i = 0; i < 6; ++i)
        {
            result.mac[i] = neighbor->mac[i];
        }
    }
    return result;
}

NeighborState ResolveNeighbor(const uint8_t* address, uint8_t* mac)
{
    uint8_t hop[4];
    if (!NextHop(address, hop) || !EqualBytes(address, hop, 4)) return NeighborState::Failed;
    auto* neighbor = Find(address);
    if (!neighbor)
    {
        // Pending resolutions are never evicted. Prefer empty, then oldest non-pending entry.
        for (auto& candidate : neighbors)
        {
            Age(candidate);
            if (candidate.state == NeighborState::Missing)
            {
                neighbor = &candidate;
                break;
            }
            if (candidate.state != NeighborState::Pending &&
                (!neighbor || tick - candidate.updated > tick - neighbor->updated))
            {
                neighbor = &candidate;
            }
        }
        if (!neighbor) return NeighborState::Pending;
        *neighbor = {};
        neighbor->state = NeighborState::Pending;
        neighbor->created = neighbor->updated = tick;
        for (unsigned i = 0; i < 4; ++i)
        {
            neighbor->address[i] = address[i];
        }
    }
    if (neighbor->state == NeighborState::Resolved)
    {
        for (unsigned i = 0; i < 6; ++i)
        {
            mac[i] = neighbor->mac[i];
        }
    }
    return neighbor->state;
}

void ReceiveNeighbor(const uint8_t* frame, const ArpPacket& packet, const uint8_t* localMac)
{
    if (packet.operation != 2 || !EqualBytes(frame, localMac, 6) ||
        !EqualBytes(packet.targetMac, localMac, 6) || !EqualBytes(packet.targetIp, LocalAddress, 4))
    {
        return;
    }
    auto* neighbor = Find(packet.senderIp);
    if (!neighbor || !neighbor->attempts ||
        (neighbor->state != NeighborState::Pending && neighbor->state != NeighborState::Failed))
    {
        return;
    }
    for (unsigned i = 0; i < 6; ++i)
    {
        neighbor->mac[i] = packet.senderMac[i];
    }
    neighbor->state = NeighborState::Resolved;
    neighbor->updated = tick;
}

void PollNeighbors(const uint8_t* localMac)
{
    for (auto& neighbor : neighbors)
    {
        Age(neighbor);
        if (neighbor.state == NeighborState::Failed && !neighbor.timeoutLogged)
        {
            if (EqualBytes(neighbor.address, GatewayAddress, 4)) SerialText("NET: ARP gateway timeout\n");
            neighbor.timeoutLogged = true;
        }
    }
    if (probed && tick - lastProbe < TimerTicksForMilliseconds(100)) return;
    for (unsigned work = 0; work < 16; ++work)
    {
        auto& neighbor = neighbors[cursor];
        cursor = (cursor + 1) % 16;
        if (neighbor.state != NeighborState::Pending || neighbor.attempts >= 5 ||
            (neighbor.attempts && tick - neighbor.lastAttempt < TimerTicksForMilliseconds(500)))
        {
            continue;
        }
        uint8_t request[ArpFrameSize];
        BuildArp(request, 1, localMac, LocalAddress, nullptr, neighbor.address);
        lastProbe = tick;
        probed = true;
        if (Transmit(request, sizeof(request)))
        {
            ++neighbor.attempts;
            neighbor.lastAttempt = tick;
            if (EqualBytes(neighbor.address, GatewayAddress, 4)) SerialText("NET: ARP probe 10.0.2.2\n");
        }
        break;
    }
}

void ResetNeighbors()
{
    for (auto& neighbor : neighbors)
    {
        neighbor = {};
    }
    // Keep the global send limiter across link flaps.
}

unsigned NeighborCount()
{
    unsigned count = 0;
    for (auto& neighbor : neighbors)
    {
        Age(neighbor);
        if (neighbor.state == NeighborState::Resolved) ++count;
    }
    return count;
}
}
