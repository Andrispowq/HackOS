#include "socket.h"
#include "udp.h"
#include "route.h"
#include "drivers/net/e1000.h"
#include "arch/x86_64/interrupts/guard.h"
#include "arch/x86_64/timer/pit.h"
#include "hackos/abi.h"

namespace Net
{
namespace
{
struct Message
{
    uint32_t size;
    uint8_t data[UdpDataMax];
};
struct Socket
{
    uint64_t owner, handle, started, sent;
    uint8_t peer[4];
    uint16_t peerPort, localPort;
    uint32_t state, head, count, dropped;
    Message transmit, receive[4];
};
Socket sockets[16];
uint64_t nextHandle, lastSend;
unsigned cursor;
uint16_t nextPort = 32768;
bool sent;

Socket* Find(uint64_t pid, uint64_t handle)
{
    for (auto& socket : sockets)
    {
        if (socket.owner == pid && socket.handle == handle && handle) return &socket;
    }
    return nullptr;
}

bool PortUsed(uint16_t port)
{
    for (const auto& socket : sockets)
    {
        if (socket.owner && socket.localPort == port) return true;
    }
    return false;
}

void Expire(Socket& socket)
{
    if (socket.state == HACKOS_UDP_PENDING && tick - socket.started >= TimerTicksForMilliseconds(3000))
    {
        socket.state = HACKOS_UDP_TIMEOUT;
        socket.transmit = {};
    }
}

void Snapshot(const Socket& socket, HackosDatagram& record)
{
    record = {};
    for (unsigned i = 0; i < 4; ++i)
    {
        record.address[i] = socket.peer[i];
    }
    record.peerPort = socket.peerPort;
    record.localPort = socket.localPort;
    record.state = socket.state;
    record.queued = socket.count;
    record.dropped = socket.dropped;
    record.sent = socket.sent;
}
}

uint64_t UdpControl(uint64_t pid, uint64_t operation, uint64_t handle, HackosDatagram& record)
{
    PreemptionGuard guard;
    if (operation == HACKOS_UDP_OPEN)
    {
        uint8_t hop[4];
        if (handle || !record.peerPort || !NextHop(record.address, hop) ||
            (record.localPort && (record.localPort < 1024 || record.localPort >= 49152)))
        {
            return HACKOS_ERROR;
        }
        unsigned owned = 0;
        Socket* free = nullptr;
        for (auto& socket : sockets)
        {
            if (socket.owner == pid) ++owned;
            if (!socket.owner && !free) free = &socket;
        }
        if (owned >= 4 || !free) return HACKOS_RETRY;
        uint16_t port = record.localPort;
        if (port && PortUsed(port)) return HACKOS_RETRY;
        if (!port)
        {
            // At most sixteen occupied ports: seventeen candidates suffice.
            for (unsigned i = 0; i < 17; ++i)
            {
                port = nextPort++;
                if (nextPort >= 49152) nextPort = 32768;
                if (!PortUsed(port)) break;
            }
        }
        if (nextHandle >= 0x7fffffffffffffffULL) return HACKOS_ERROR;
        *free = {};
        free->owner = pid;
        free->handle = ++nextHandle;
        free->localPort = port;
        free->peerPort = record.peerPort;
        for (unsigned i = 0; i < 4; ++i)
        {
            free->peer[i] = record.address[i];
        }
        return free->handle;
    }
    auto* socket = Find(pid, handle);
    if (!socket) return HACKOS_NOT_FOUND;
    Expire(*socket);
    switch (operation)
    {
    case HACKOS_UDP_CLOSE:
        *socket = {};
        return 0;
    case HACKOS_UDP_SEND:
        if (record.size > UdpDataMax) return HACKOS_ERROR;
        if (socket->state == HACKOS_UDP_PENDING) return HACKOS_RETRY;
        socket->transmit = {};
        socket->transmit.size = record.size;
        for (unsigned i = 0; i < record.size; ++i)
        {
            socket->transmit.data[i] = record.data[i];
        }
        socket->started = tick;
        socket->state = HACKOS_UDP_PENDING;
        return 0;
    case HACKOS_UDP_STATUS:
        Snapshot(*socket, record);
        return 0;
    case HACKOS_UDP_RECEIVE:
        if (!socket->count) return HACKOS_RETRY;
        Snapshot(*socket, record);
        record.size = socket->receive[socket->head].size;
        for (unsigned i = 0; i < record.size; ++i)
        {
            record.data[i] = socket->receive[socket->head].data[i];
        }
        return 0;
    default:
        return HACKOS_ERROR;
    }
}

void ConsumeUdp(uint64_t pid, uint64_t handle)
{
    PreemptionGuard guard;
    auto* socket = Find(pid, handle);
    if (!socket || !socket->count) return;
    socket->receive[socket->head] = {};
    socket->head = (socket->head + 1) % 4;
    --socket->count;
}

void CloseUdpSockets(uint64_t pid)
{
    PreemptionGuard guard;
    for (auto& socket : sockets)
    {
        if (socket.owner == pid) socket = {};
    }
}

void PollUdpSockets(bool ready, bool link, const uint8_t* mac, const uint8_t* ip)
{
    // First update all deadlines, even if one socket transmits below.
    for (auto& socket : sockets)
    {
        if (!socket.owner) continue;
        Expire(socket);
        if (socket.state == HACKOS_UDP_PENDING && (!ready || !link))
        {
            socket.state = HACKOS_UDP_UNAVAILABLE;
            socket.transmit = {};
        }
    }
    if (!ready || !link) return;
    for (unsigned work = 0; work < 16; ++work)
    {
        auto& socket = sockets[cursor];
        cursor = (cursor + 1) % 16;
        if (!socket.owner || socket.state != HACKOS_UDP_PENDING) continue;
        uint8_t hop[4], peer[6];
        if (!NextHop(socket.peer, hop)) continue;
        auto neighbor = ResolveNeighbor(hop, peer);
        if (neighbor == NeighborState::Failed)
        {
            socket.state = HACKOS_UDP_NEIGHBOR_FAILED;
            socket.transmit = {};
            continue;
        }
        if (neighbor != NeighborState::Resolved || (sent && tick - lastSend < TimerTicksForMilliseconds(50))) continue;
        uint8_t frame[UdpFrameMax];
        unsigned size = BuildUdp(frame, mac, peer, ip, socket.peer, socket.localPort, socket.peerPort,
                                 socket.transmit.data, socket.transmit.size);
        lastSend = tick;
        sent = true;
        if (size && Transmit(frame, size))
        {
            socket.state = HACKOS_UDP_SENT;
            socket.transmit = {};
            ++socket.sent;
        }
        break;
    }
}

void ReceiveUdpSockets(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip)
{
    UdpPacket packet;
    if (!ParseUdp(frame, length, mac, ip, packet)) return;
    for (auto& socket : sockets)
    {
        if (!socket.owner || socket.localPort != packet.destinationPort || socket.peerPort != packet.sourcePort ||
            !EqualBytes(socket.peer, packet.source, 4))
        {
            continue;
        }
        if (socket.count == 4)
        {
            if (socket.dropped != 0xffffffff) ++socket.dropped;
            return;
        }
        auto& message = socket.receive[(socket.head + socket.count) % 4];
        message = {};
        message.size = packet.size;
        for (unsigned i = 0; i < packet.size; ++i)
        {
            message.data[i] = packet.data[i];
        }
        ++socket.count;
        return;
    }
}
}
