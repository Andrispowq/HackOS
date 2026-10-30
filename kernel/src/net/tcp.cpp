#include "tcp.h"

void WakeStreamWaiters();
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
    uint64_t handle, progress, persistAt, totalSent, totalReceived;
    uint32_t txBase, txSize;
    bool stream, shutdown;
    Phase phase;
    uint32_t initial, una, next, receive, windowSequence, windowAck;
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
uint64_t nextHandle;

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
    WakeStreamWaiters();
}

void Expire()
{
    for (auto& c : connections)
    {
        if (c.phase == Free) continue;
        if (c.result.state == HACKOS_TCP_PENDING &&
            ((!c.stream && tick - c.started >= TimerTicksForMilliseconds(20000)) ||
             (c.stream && (c.phase == Syn || c.txSize || c.shutdown) &&
              tick - c.progress >= TimerTicksForMilliseconds(20000))))
        {
            Fail(c, HACKOS_TCP_TIMEOUT);
        }
        if (!c.stream && c.result.state != HACKOS_TCP_PENDING && tick - c.finished >= TimerTicksForMilliseconds(10000)) c.owner = 0;
        if (c.phase == TimeWait)
        {
            if (tick - c.lastActivity >= TimerTicksForMilliseconds(120000))
            {
                if (c.stream && c.owner)
                {
                    c.phase = Done;
                }
                else
                {
                    c = {};
                }
            }
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

static uint64_t TcpControlImpl(uint64_t pid, uint64_t operation, HackosTcp& record, bool stream, Connection** created)
{
    PreemptionGuard guard;
    if (!pid) return HACKOS_ERROR;
    Expire();
    if (operation == HACKOS_TCP_CANCEL)
    {
        for (auto& c : connections)
        {
            if (c.owner != pid || c.stream) continue;
            c.owner = 0;
            if (c.phase == TimeWait) continue;
            if (c.result.state == HACKOS_TCP_PENDING) Fail(c, HACKOS_TCP_RESET);
            if (c.phase == Done) c = {};
        }
        return 0;
    }
    Connection* found = nullptr;
    Connection* free = nullptr;
    unsigned owners = 0, streamsOwned = 0;
    for (auto& c : connections)
    {
        if (c.owner == pid && !c.stream) found = &c;
        if (c.owner == pid && c.stream) ++streamsOwned;
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
    if ((!stream && found) || (stream && streamsOwned >= 4) || owners >= 8 || !free || (startedOnce && tick - lastStart < TimerTicksForMilliseconds(100))) return HACKOS_RETRY;
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
    c.started = c.progress = tick;
    c.stream = stream;
    c.shutdown = !stream;
    c.initial = ((uint32_t)random[0] << 24) | ((uint32_t)random[1] << 16) | ((uint32_t)random[2] << 8) | random[3];
    c.una = c.next = c.initial;
    c.txBase = c.initial + 1;
    c.txSize = record.size;
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
    if (created) *created = &c;
    return 0;
}

uint64_t TcpControl(uint64_t pid, uint64_t operation, HackosTcp& record)
{
    return TcpControlImpl(pid, operation, record, false, nullptr);
}

namespace
{
Connection* FindStream(uint64_t pid, uint64_t handle)
{
    if (!pid || !handle) return nullptr;
    for (auto& c : connections)
    {
        if (c.stream && c.owner == pid && c.handle == handle) return &c;
    }
    return nullptr;
}

void StreamSnapshot(const Connection& c, HackosTcpStream& record)
{
    record = {};
    for (unsigned i = 0; i < 4; ++i)
    {
        record.address[i] = c.result.address[i];
    }
    record.peerPort = c.result.peerPort;
    record.localPort = c.result.localPort;
    record.state = c.result.state;
    record.queued = c.result.received;
    record.pending = c.txSize ? c.txSize - (c.una - c.txBase) : 0;
    record.retransmissions = c.result.retransmissions;
    record.sent = c.totalSent;
    record.received = c.totalReceived;
    if (c.phase == Exchange) record.flags |= HACKOS_TCP_CONNECTED;
    if (c.phase == Exchange && !c.shutdown && !c.txSize) record.flags |= HACKOS_TCP_WRITABLE;
    if (c.finReceived) record.flags |= HACKOS_TCP_EOF;
    if (c.shutdown) record.flags |= HACKOS_TCP_WRITE_CLOSED;
}
}

uint64_t TcpStreamControl(uint64_t pid, uint64_t operation, uint64_t handle, HackosTcpStream& record)
{
    PreemptionGuard guard;
    if (!pid) return HACKOS_ERROR;
    Expire();
    if (operation == HACKOS_TCP_CONNECT)
    {
        if (handle || record.localPort || record.size || nextHandle >= 0x7fffffffffffffffULL) return HACKOS_ERROR;
        HackosTcp request = {};
        for (unsigned i = 0; i < 4; ++i)
        {
            request.address[i] = record.address[i];
        }
        request.peerPort = record.peerPort;
        Connection* c = nullptr;
        uint64_t status = TcpControlImpl(pid, HACKOS_TCP_START, request, true, &c);
        if (status) return status;
        c->handle = ++nextHandle;
        return c->handle;
    }
    auto* c = FindStream(pid, handle);
    if (!c) return HACKOS_NOT_FOUND;
    if (operation == HACKOS_TCP_STATUS)
    {
        StreamSnapshot(*c, record);
        return 0;
    }
    if (operation == HACKOS_TCP_CLOSE)
    {
        c->owner = 0;
        if (c->phase != TimeWait && c->result.state == HACKOS_TCP_PENDING) Fail(*c, HACKOS_TCP_RESET);
        if (c->phase == Done) *c = {};
        WakeStreamWaiters();
        return 0;
    }
    if (operation == HACKOS_TCP_READ)
    {
        unsigned capacity = record.size;
        if (capacity > sizeof(record.data)) return HACKOS_ERROR;
        StreamSnapshot(*c, record);
        if (!capacity) return 0;
        unsigned size = c->result.received;
        if (size > capacity) size = capacity;
        if (!size)
        {
            if (c->result.state != HACKOS_TCP_PENDING && c->result.state != HACKOS_TCP_COMPLETE) return HACKOS_ERROR;
            return c->finReceived ? 0 : HACKOS_RETRY;
        }
        record.size = size;
        for (unsigned i = 0; i < size; ++i)
        {
            record.data[i] = c->result.data[i];
        }
        return 0;
    }
    if (operation == HACKOS_TCP_WRITE || operation == HACKOS_TCP_SHUTDOWN_WRITE)
    {
        if (c->result.state != HACKOS_TCP_PENDING) return HACKOS_ERROR;
        if (c->phase == Syn) return HACKOS_RETRY;
        if (c->phase != Exchange) return HACKOS_ERROR;
        if (operation == HACKOS_TCP_SHUTDOWN_WRITE)
        {
            if (!c->shutdown) c->progress = tick;
            c->shutdown = true;
            WakeStreamWaiters();
            return 0;
        }
        if (c->shutdown || record.size > sizeof(c->outgoing)) return HACKOS_ERROR;
        if (!record.size) return 0;
        if (c->txSize) return HACKOS_RETRY;
        c->txBase = c->next;
        c->txSize = record.size;
        for (unsigned i = 0; i < record.size; ++i)
        {
            c->outgoing[i] = record.data[i];
        }
        c->progress = c->persistAt = tick;
        return 0;
    }
    return HACKOS_ERROR;
}

void ConsumeTcp(uint64_t pid, uint64_t handle, unsigned size)
{
    auto* c = FindStream(pid, handle);
    if (!c || size > c->result.received || !size) return;
    c->result.received -= size;
    for (unsigned i = 0; i < c->result.received; ++i)
    {
        c->result.data[i] = c->result.data[i + size];
    }
    if (c->phase == Exchange) c->ack = true; // Publish the reopened receive window.
    WakeStreamWaiters();
}

uint32_t TcpReadiness(uint64_t pid, uint64_t handle, uint32_t events)
{
    auto* c = FindStream(pid, handle);
    if (!c) return HACKOS_POLL_INVALID;
    uint32_t ready = 0;
    if (c->result.state != HACKOS_TCP_PENDING && c->result.state != HACKOS_TCP_COMPLETE) ready |= HACKOS_POLL_ERROR;
    if (c->finReceived) ready |= HACKOS_POLL_HANGUP;
    if (c->result.received || c->finReceived) ready |= events & HACKOS_POLL_READ;
    if (c->phase == Exchange && !c->shutdown && !c->txSize) ready |= events & HACKOS_POLL_WRITE;
    return ready;
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
            c.windowSequence = p.sequence;
            c.windowAck = p.acknowledgement;
            c.mss = p.mss < 512 ? p.mss : 512;
            c.phase = Exchange;
            c.progress = tick;
            WakeStreamWaiters();
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
            if (p.sequence == c.receive)
            {
                Fail(c, HACKOS_TCP_RESET, false);
            }
            else if (p.sequence - c.receive < sizeof(c.result.data) - c.result.received)
            {
                c.ack = true;
            }
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
        if (Before(c.windowSequence, p.sequence) ||
            (c.windowSequence == p.sequence && !Before(p.acknowledgement, c.windowAck)))
        {
            c.window = p.window;
            c.windowSequence = p.sequence;
            c.windowAck = p.acknowledgement;
        }
        if (Before(c.una, p.acknowledgement))
        {
            if (c.stream && !c.finSent) c.totalSent += p.acknowledgement - c.una;
            c.una = p.acknowledgement;
            c.progress = tick;
            if (c.stream && !c.finSent && c.una == c.txBase + c.txSize)
            {
                c.txSize = 0;
                c.txBase = c.una;
                WakeStreamWaiters();
            }
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
                if (!c.stream)
                {
                    Fail(c, HACKOS_TCP_TOO_LARGE);
                    return;
                }
                size = sizeof(c.result.data) - c.result.received;
            }
            for (unsigned i = 0; i < size; ++i)
            {
                c.result.data[c.result.received++] = p.data[skip + i];
            }
            c.receive += size;
            c.totalReceived += size;
            if (size) WakeStreamWaiters();
        }
        if (!c.finReceived && (p.flags & TcpFin) && p.sequence + p.size == c.receive &&
            (!c.stream || c.result.received < sizeof(c.result.data)))
        {
            ++c.receive;
            c.finReceived = true;
            WakeStreamWaiters();
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
        bool retry = false, consumes = false, probe = false;
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
                offset = c.una - c.txBase;
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
            offset = c.una - c.txBase;
            size = c.txSize - offset;
            if (size)
            {
                if (!c.window)
                {
                    if (!c.stream || tick - c.persistAt < TimerTicksForMilliseconds(1000)) continue;
                    // An old-sequence empty segment elicits the peer's current window.
                    probe = true;
                    sequence = c.next - 1;
                    size = 0;
                }
                if (size > c.mss) size = c.mss;
                if (size > c.window) size = c.window;
                if (size) flags |= TcpPush;
            }
            else
            {
                if (!c.shutdown) continue;
                if (c.stream && !c.window)
                {
                    if (tick - c.persistAt < TimerTicksForMilliseconds(1000)) continue;
                    probe = true;
                    sequence = c.next - 1;
                }
                else
                {
                    flags |= TcpFin;
                }
            }
            consumes = !probe;
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
        else if (probe)
        {
            c.persistAt = tick;
        }
        else if (c.ack)
        {
            c.ack = false;
            if (c.phase == TimeWait && c.result.state == HACKOS_TCP_PENDING)
            {
                c.result.state = HACKOS_TCP_COMPLETE;
                c.finished = tick;
                WakeStreamWaiters();
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
        if (c.stream) c.result.received = 0;
    }
}
}
