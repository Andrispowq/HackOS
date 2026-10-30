#include "dns.h"
#include "udp.h"
#include "route.h"
#include "drivers/net/e1000.h"
#include "arch/x86_64/interrupts/guard.h"
#include "arch/x86_64/timer/pit.h"
#include "security/crypto.h"
#include "hackos/abi.h"
#include "hackos/runtime/hostname.h"

namespace Net
{
namespace
{
const uint8_t* server = DnsAddress;
struct Lookup
{
    uint64_t owner, started, lastAttempt, finished;
    unsigned attempts;
    uint16_t id, port;
    HackosResolve result;
};
Lookup lookups[8];
uint64_t lastSend;
bool sent;
unsigned cursor;

void Expire()
{
    for (auto& lookup : lookups)
    {
        if (!lookup.owner) continue;
        if (lookup.result.state == HACKOS_RESOLVE_PENDING &&
            tick - lookup.started >= TimerTicksForMilliseconds(5000))
        {
            lookup.result.state = HACKOS_RESOLVE_TIMEOUT;
            lookup.finished = tick;
        }
        if (lookup.result.state != HACKOS_RESOLVE_PENDING &&
            tick - lookup.finished >= TimerTicksForMilliseconds(10000))
        {
            lookup = {};
        }
    }
}
}

void CancelResolve(uint64_t pid)
{
    PreemptionGuard guard;
    for (auto& lookup : lookups)
    {
        if (lookup.owner == pid) lookup = {};
    }
}

uint64_t ResolveControl(uint64_t pid, uint64_t operation, HackosResolve& record)
{
    PreemptionGuard guard;
    Expire();
    if (operation == HACKOS_RESOLVE_CANCEL)
    {
        CancelResolve(pid);
        return 0;
    }
    Lookup* found = nullptr;
    Lookup* free = nullptr;
    for (auto& lookup : lookups)
    {
        if (lookup.owner == pid) found = &lookup;
        if (!lookup.owner && !free) free = &lookup;
    }
    if (operation == HACKOS_RESOLVE_QUERY)
    {
        if (!found) return HACKOS_NOT_FOUND;
        record = found->result;
        return 0;
    }
    char name[256];
    if (operation != HACKOS_RESOLVE_START || !Hackos::NormalizeHostname(record.name, name)) return HACKOS_ERROR;
    if (found && found->result.state == HACKOS_RESOLVE_PENDING) return HACKOS_RETRY;
    if (!found) found = free;
    if (!found) return HACKOS_RETRY;
    uint8_t random[16];
    if (!RandomSalt(random)) return HACKOS_ERROR;
    uint16_t port = 49152 + ((((unsigned)random[0] << 8) | random[1]) & 16383);
    for (const auto& lookup : lookups)
    {
        if (lookup.owner && lookup.port == port) return HACKOS_RETRY;
    }
    *found = {};
    found->owner = pid;
    found->started = tick;
    found->port = port;
    found->id = ((uint16_t)random[2] << 8) | random[3];
    for (unsigned i = 0; i < 256; ++i)
    {
        found->result.name[i] = name[i];
        if (!name[i]) break;
    }
    return 0;
}

void PollResolve(bool ready, bool link, const uint8_t* mac, const uint8_t* ip)
{
    Expire();
    for (unsigned work = 0; work < 8; ++work)
    {
        auto& lookup = lookups[cursor];
        cursor = (cursor + 1) % 8;
        if (!lookup.owner || lookup.result.state != HACKOS_RESOLVE_PENDING) continue;
        if (!ready || !link)
        {
            lookup.result.state = HACKOS_RESOLVE_UNAVAILABLE;
            lookup.finished = tick;
            continue;
        }
        uint8_t hop[4], peer[6];
        if (!NextHop(server, hop)) continue;
        auto state = ResolveNeighbor(hop, peer);
        if (state == NeighborState::Failed)
        {
            lookup.result.state = HACKOS_RESOLVE_NEIGHBOR_FAILED;
            lookup.finished = tick;
            continue;
        }
        if (state != NeighborState::Resolved || lookup.attempts >= 3 ||
            (lookup.attempts && tick - lookup.lastAttempt < TimerTicksForMilliseconds(1000)) ||
            (sent && tick - lastSend < TimerTicksForMilliseconds(100)))
        {
            continue;
        }
        uint8_t data[512], frame[UdpFrameMax];
        unsigned size = BuildDns(data, lookup.result.name, lookup.id);
        unsigned length = BuildUdp(frame, mac, peer, ip, server, lookup.port, 53, data, size);
        lastSend = tick;
        sent = true;
        if (length && Transmit(frame, length))
        {
            lookup.lastAttempt = tick;
            ++lookup.attempts;
        }
        break;
    }
}

void ReceiveResolve(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip)
{
    Expire();
    UdpPacket packet;
    if (!ParseUdp(frame, length, mac, ip, packet) || packet.sourcePort != 53 || !EqualBytes(packet.source, server, 4)) return;
    for (auto& lookup : lookups)
    {
        if (!lookup.owner || !lookup.attempts || lookup.result.state != HACKOS_RESOLVE_PENDING ||
            lookup.port != packet.destinationPort)
        {
            continue;
        }
        HackosResolve result = lookup.result;
        if (ParseDns(packet.data, packet.size, lookup.id, lookup.result.name, result))
        {
            lookup.result = result;
            lookup.finished = tick;
        }
        return;
    }
}
}

namespace Net
{
void InvalidateResolve()
{
    for (auto& lookup : lookups)
    {
        if (lookup.owner && lookup.result.state == HACKOS_RESOLVE_PENDING)
        {
            lookup.result.state = HACKOS_RESOLVE_UNAVAILABLE;
            lookup.finished = tick;
        }
    }
}
}
