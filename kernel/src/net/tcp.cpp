#include "tcp.h"
#include "route.h"
#include "drivers/net/e1000.h"
#include "hackos/abi.h"
#include "security/crypto.h"
#include "arch/x86_64/interrupts/guard.h"
#include "arch/x86_64/timer/pit.h"

namespace Net
{
namespace
{
enum Phase { Free, Syn, Exchange, TimeWait, Reset, Done };
struct Connection
{
    uint64_t owner, started, sentAt, finished, lastActivity;
    Phase phase;
    uint32_t initial, una, next, receive;
    uint16_t window, mss;
    unsigned tries;
    bool transmitted, ack, finSent, finReceived;
    uint8_t outgoing[512];
    HackosTcp result;
};
// Includes TIME-WAIT tuples, which cannot be reused for 120 seconds.
Connection connections[32];
uint16_t nextPort = 49152;
uint64_t lastStart, lastWire;
bool startedOnce, wired;
unsigned cursor;

bool Before(uint32_t a, uint32_t b)
{
    return (int32_t)(a - b) < 0;
}

void Fail(Connection& c, uint32_t state, bool reset = true)
{
    c.result.state = state;
    c.finished = tick;
    c.phase = reset && c.transmitted ? Reset : Done;
    c.ack = false;
}

void Expire()
{
    for (auto& c : connections)
    {
        if (c.phase == Free) continue;
        if (c.result.state == HACKOS_TCP_PENDING && tick - c.started >= TimerTicksForMilliseconds(20000))
        {
            Fail(c, HACKOS_TCP_TIMEOUT);
        }
        if (c.result.state != HACKOS_TCP_PENDING && tick - c.finished >= TimerTicksForMilliseconds(10000)) c.owner = 0;
        if (c.phase == TimeWait)
        {
            if (tick - c.lastActivity >= TimerTicksForMilliseconds(120000)) c = {};
        }
        else if (!c.owner && c.phase == Done)
        {
            c = {};
        }
    }
}

void Complete(Connection& c)
{
    if (!c.finSent || !c.finReceived || c.una != c.next) return;
    c.phase = TimeWait;
    c.lastActivity = tick;
    // Completion is published only after the final ACK reaches the NIC.
    c.ack = true;
}
}

void CancelTcp(uint64_t pid)
{
    PreemptionGuard guard;
    for (auto& c : connections)
    {
        if (c.owner != pid) continue;
        c.owner = 0;
        if (c.phase == TimeWait) continue;
        if (c.result.state == HACKOS_TCP_PENDING) Fail(c, HACKOS_TCP_RESET);
        if (c.phase == Done) c = {};
    }
}

uint64_t TcpControl(uint64_t pid, uint64_t operation, HackosTcp& record)
{
    PreemptionGuard guard;
    if (!pid) return HACKOS_ERROR;
    Expire();
    if (operation == HACKOS_TCP_CANCEL)
    {
        CancelTcp(pid);
        return 0;
    }
    Connection* found = nullptr;
    Connection* free = nullptr;
    unsigned owners = 0;
    for (auto& c : connections)
    {
        if (c.owner == pid) found = &c;
        if (c.owner) ++owners;
        if (c.phase == Free && !free) free = &c;
    }
    if (operation == HACKOS_TCP_QUERY)
    {
        if (!found) return HACKOS_NOT_FOUND;
        record = found->result;
        return 0;
    }
    uint8_t hop[4], random[16];
    if (operation != HACKOS_TCP_START || !pid || record.size > 512 || !record.peerPort || record.localPort ||
        !NextHop(record.address, hop))
    {
        return HACKOS_ERROR;
    }
    if (found || owners >= 8 || !free || (startedOnce && tick - lastStart < TimerTicksForMilliseconds(100))) return HACKOS_RETRY;
    if (!RandomSalt(random)) return HACKOS_ERROR;
    uint16_t port = 0;
    for (unsigned attempt = 0; attempt < 33 && !port; ++attempt)
    {
        uint16_t candidate = nextPort++;
        if (nextPort < 49152) nextPort = 49152;
        bool busy = false;
        for (const auto& c : connections)
        {
            if (c.phase != Free && c.result.localPort == candidate) busy = true;
        }
        if (!busy) port = candidate;
    }
    if (!port) return HACKOS_RETRY;
    auto& c = *free;
    c = {};
    c.owner = pid;
    c.phase = Syn;
    c.started = tick;
    c.initial = ((uint32_t)random[0] << 24) | ((uint32_t)random[1] << 16) | ((uint32_t)random[2] << 8) | random[3];
    c.una = c.next = c.initial;
    c.result.peerPort = record.peerPort;
    c.result.localPort = port;
    c.result.size = record.size;
    for (unsigned i = 0; i < 4; ++i)
    {
        c.result.address[i] = record.address[i];
    }
    for (unsigned i = 0; i < record.size; ++i)
    {
        c.outgoing[i] = record.data[i];
    }
    lastStart = tick;
    startedOnce = true;
    return 0;
}

void ReceiveTcp(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip)
{
    Expire();
    TcpPacket p;
    if (!ParseTcp(frame, length, mac, ip, p)) return;
    for (auto& c : connections)
    {
        if ((c.phase != Syn && c.phase != Exchange && c.phase != TimeWait) || !c.transmitted ||
            p.sourcePort != c.result.peerPort || p.destinationPort != c.result.localPort || !EqualBytes(p.source, c.result.address, 4))
        {
            continue;
        }
        if (c.phase == Syn)
        {
            if (!(p.flags & TcpAck) || p.acknowledgement != c.next || c.next != c.initial + 1) return;
            if (p.flags & TcpRst)
            {
                Fail(c, HACKOS_TCP_RESET, false);
                return;
            }
            if (!(p.flags & TcpSyn) || (p.flags & TcpFin) || p.size) return;
            c.receive = p.sequence + 1;
            c.una = c.next;
            c.window = p.window;
            c.mss = p.mss < 512 ? p.mss : 512;
            c.phase = Exchange;
            c.tries = 0;
            c.ack = true;
            return;
        }
        if (c.phase == TimeWait)
        {
            if ((p.flags & (TcpFin | TcpRst | TcpSyn)) == TcpFin && p.sequence + p.size + 1 == c.receive)
            {
                c.ack = true;
                c.lastActivity = tick;
            }
            return;
        }
        if (p.flags & TcpRst)
        {
            if (p.sequence == c.receive) Fail(c, HACKOS_TCP_RESET, false);
            return;
        }
        if (p.flags & TcpSyn)
        {
            c.ack = true;
            return;
        }
        if (!(p.flags & TcpAck)) return;
        if (Before(c.next, p.acknowledgement))
        {
            c.ack = true;
            return;
        }
        uint32_t end = p.sequence + p.size + ((p.flags & TcpFin) ? 1 : 0);
        if (Before(c.receive, p.sequence) || Before(end, c.receive))
        {
            c.ack = true;
            return;
        }
        if (Before(p.acknowledgement, c.una)) return;
        c.window = p.window;
        if (Before(c.una, p.acknowledgement))
        {
            c.una = p.acknowledgement;
            c.tries = 0;
            c.sentAt = tick;
        }
        unsigned skip = c.receive - p.sequence;
        if (skip > p.size) skip = p.size;
        unsigned size = p.size - skip;
        if (!c.finReceived && size)
        {
            if (size > sizeof(c.result.data) - c.result.received)
            {
                Fail(c, HACKOS_TCP_TOO_LARGE);
                return;
            }
            for (unsigned i = 0; i < size; ++i)
            {
                c.result.data[c.result.received++] = p.data[skip + i];
            }
            c.receive += size;
        }
        if (!c.finReceived && (p.flags & TcpFin) && p.sequence + p.size == c.receive)
        {
            ++c.receive;
            c.finReceived = true;
        }
        if (p.size || (p.flags & TcpFin)) c.ack = true;
        Complete(c);
        return;
    }
}

void PollTcp(bool ready, bool link, const uint8_t* mac, const uint8_t* ip)
{
    Expire();
    if (!ready || !link)
    {
        InvalidateTcp();
        return;
    }
    if (wired && tick - lastWire < TimerTicksForMilliseconds(10)) return;
    for (unsigned n = 0; n < 32; ++n)
    {
        unsigned index = (cursor + n) % 32;
        auto& c = connections[index];
        if (c.phase == Free || c.phase == Done || (c.phase == TimeWait && !c.ack)) continue;
        uint8_t peerMac[6], hop[4];
        if (!NextHop(c.result.address, hop))
        {
            Fail(c, HACKOS_TCP_UNAVAILABLE, false);
            continue;
        }
        auto neighbor = ResolveNeighbor(hop, peerMac);
        if (neighbor == NeighborState::Failed)
        {
            Fail(c, HACKOS_TCP_NEIGHBOR_FAILED, false);
            continue;
        }
        if (neighbor != NeighborState::Resolved) continue;
        uint8_t flags = TcpAck;
        uint32_t sequence = c.next;
        unsigned size = 0, offset = 0;
        bool retry = false, consumes = false;
        if (c.phase == Reset)
        {
            flags = TcpRst | TcpAck;
        }
        else if (c.ack)
        {
            // ACKs are coalesced, including duplicates and out-of-order segments.
        }
        else if (c.una != c.next)
        {
            unsigned delay = 1000u << (c.tries < 3 ? c.tries : 3);
            if (tick - c.sentAt < TimerTicksForMilliseconds(delay)) continue;
            retry = true;
            sequence = c.una;
            if (c.phase == Syn)
            {
                flags = TcpSyn;
            }
            else if (c.finSent)
            {
                flags |= TcpFin;
            }
            else
            {
                offset = c.una - (c.initial + 1);
                size = c.next - c.una;
                flags |= TcpPush;
            }
        }
        else if (c.phase == Syn)
        {
            flags = TcpSyn;
            consumes = true;
        }
        else if (!c.finSent)
        {
            offset = c.una - (c.initial + 1);
            size = c.result.size - offset;
            if (size)
            {
                if (!c.window) continue; // Bounded exchange times out if the peer never opens its window.
                if (size > c.mss) size = c.mss;
                if (size > c.window) size = c.window;
                flags |= TcpPush;
            }
            else
            {
                flags |= TcpFin;
            }
            consumes = true;
        }
        else
        {
            continue;
        }
        uint8_t frame[14 + 20 + 24 + 512];
        unsigned length = BuildTcp(frame, mac, peerMac, ip, c.result.address, c.result.localPort, c.result.peerPort,
                                   sequence, flags & TcpAck ? c.receive : 0, flags,
                                   sizeof(c.result.data) - c.result.received, c.outgoing + offset, size);
        lastWire = tick;
        wired = true;
        cursor = (index + 1) % 32;
        if (!Transmit(frame, length)) return;
        c.transmitted = true;
        if (c.phase == Reset)
        {
            c.phase = Done;
        }
        else if (c.ack)
        {
            c.ack = false;
            if (c.phase == TimeWait && c.result.state == HACKOS_TCP_PENDING)
            {
                c.result.state = HACKOS_TCP_COMPLETE;
                c.finished = tick;
            }
        }
        else
        {
            c.sentAt = tick;
            if (retry)
            {
                ++c.tries;
                ++c.result.retransmissions;
            }
            if (consumes)
            {
                c.next += size + ((flags & (TcpSyn | TcpFin)) ? 1 : 0);
                c.tries = 0;
                if (flags & TcpFin) c.finSent = true;
            }
        }
        return;
    }
}

void InvalidateTcp()
{
    for (auto& c : connections)
    {
        if (c.phase == Free) continue;
        if (c.result.state == HACKOS_TCP_PENDING) Fail(c, HACKOS_TCP_UNAVAILABLE, false);
        // Never emit old-tuple ACKs or RSTs using a newly leased address.
        c.phase = Done;
        c.ack = false;
    }
}
}
