#include "ping.h"
#include "icmp.h"
#include "arp.h"
#include "route.h"
#include "drivers/net/e1000.h"
#include "hackos/abi.h"
#include "arch/x86_64/interrupts/guard.h"
#include "arch/x86_64/timer/pit.h"

namespace Net
{
namespace
{
struct Request
{
    uint64_t owner, token, started, sentAt, finished;
    bool sent;
    uint16_t checksum;
    HackosEcho result;
    uint8_t data[EchoDataSize];
};
Request requests[8];
uint64_t nextToken, lastSend;
bool hasSent;

void Expire()
{
    uint64_t now = tick;
    for (auto& request : requests)
    {
        if (!request.owner) continue;
        if (request.result.state == HACKOS_ECHO_PENDING &&
            now - request.started >= TimerTicksForMilliseconds(3000))
        {
            request.result.state = HACKOS_ECHO_TIMEOUT;
            request.finished = now;
        }
        if (request.result.state != HACKOS_ECHO_PENDING &&
            now - request.finished >= TimerTicksForMilliseconds(10000))
        {
            request = {};
        }
    }
}
}

void CancelEcho(uint64_t pid)
{
    PreemptionGuard guard;
    for (auto& request : requests)
    {
        if (request.owner == pid) request = {};
    }
}

uint64_t EchoControl(uint64_t pid, uint64_t operation, HackosEcho& record)
{
    PreemptionGuard guard;
    Expire();
    if (operation == HACKOS_ECHO_CANCEL)
    {
        CancelEcho(pid);
        return 0;
    }
    Request* found = nullptr;
    Request* free = nullptr;
    for (auto& request : requests)
    {
        if (request.owner == pid) found = &request;
        if (!request.owner && !free) free = &request;
    }
    if (operation == HACKOS_ECHO_QUERY)
    {
        if (!found) return HACKOS_NOT_FOUND;
        record = found->result;
        return 0;
    }
    uint8_t hop[4];
    if (operation != HACKOS_ECHO_START || !NextHop(record.address, hop)) return HACKOS_ERROR;
    if (found && found->result.state == HACKOS_ECHO_PENDING) return HACKOS_RETRY;
    if (!found) found = free;
    if (!found) return HACKOS_RETRY;
    *found = {};
    found->owner = pid;
    found->token = ++nextToken;
    found->started = tick;
    found->result.sequence = (uint16_t)found->token;
    for (unsigned i = 0; i < 4; ++i)
    {
        found->result.address[i] = record.address[i];
    }
    for (unsigned i = 0; i < EchoDataSize; ++i)
    {
        found->data[i] = i < 8 ? (uint8_t)(found->token >> (i * 8)) : (uint8_t)i;
    }
    return 0;
}

void ReceiveEcho(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip)
{
    Expire();
    EchoPacket packet;
    if (!ParseEchoReply(frame, length, mac, ip, packet))
    {
        EchoError error;
        if (!ParseEchoError(frame, length, mac, ip, error)) return;
        for (auto& request : requests)
        {
            if (!request.owner || !request.sent || request.result.state != HACKOS_ECHO_PENDING ||
                error.identifier != (uint16_t)(request.token >> 16) || error.sequence != (uint16_t)request.token ||
                error.checksum != request.checksum || !EqualBytes(error.destination, request.result.address, 4) ||
                !EqualBytes(error.data, request.data, error.dataSize))
            {
                continue;
            }
            request.result.state = error.type == 3 ? HACKOS_ECHO_UNREACHABLE : HACKOS_ECHO_TIME_EXCEEDED;
            request.result.errorCode = error.code;
            request.result.elapsedMs = (tick - request.sentAt) * 1000 / TimerFrequency();
            request.finished = tick;
            return;
        }
        return;
    }
    for (auto& request : requests)
    {
        if (!request.owner || !request.sent || request.result.state != HACKOS_ECHO_PENDING ||
            packet.identifier != (uint16_t)(request.token >> 16) ||
            packet.sequence != (uint16_t)request.token || !EqualBytes(packet.source, request.result.address, 4) ||
            !EqualBytes(packet.data, request.data, EchoDataSize))
        {
            continue;
        }
        request.result.state = HACKOS_ECHO_REPLY;
        request.result.ttl = packet.ttl;
        request.result.elapsedMs = (tick - request.sentAt) * 1000 / TimerFrequency();
        request.finished = tick;
        return;
    }
}

void PollEcho(bool ready, bool link, const uint8_t* mac, const uint8_t* ip)
{
    Expire();
    for (auto& request : requests)
    {
        if (!request.owner || request.result.state != HACKOS_ECHO_PENDING || request.sent) continue;
        if (!ready || !link)
        {
            request.result.state = HACKOS_ECHO_UNAVAILABLE;
            request.finished = tick;
            continue;
        }
        uint8_t hop[4], peerMac[6];
        if (!NextHop(request.result.address, hop)) continue;
        auto neighbor = ResolveNeighbor(hop, peerMac);
        if (neighbor == NeighborState::Failed)
        {
            request.result.state = HACKOS_ECHO_NEIGHBOR_FAILED;
            request.finished = tick;
            continue;
        }
        // Global 10 packets/second ceiling, including cancellation/restart loops.
        if (neighbor != NeighborState::Resolved ||
            (hasSent && tick - lastSend < TimerTicksForMilliseconds(100)))
        {
            continue;
        }
        uint8_t frame[EchoFrameSize];
        BuildEcho(frame, mac, peerMac, ip, request.result.address, (uint16_t)(request.token >> 16),
                  (uint16_t)request.token, request.data);
        if (Transmit(frame, sizeof(frame)))
        {
            request.checksum = ((uint16_t)frame[36] << 8) | frame[37];
            request.sent = true;
            request.sentAt = lastSend = tick;
            hasSent = true;
        }
        break;
    }
}
}

namespace Net
{
void InvalidateEcho()
{
    for (auto& request : requests)
    {
        if (request.owner && request.result.state == HACKOS_ECHO_PENDING)
        {
            request.result.state = HACKOS_ECHO_UNAVAILABLE;
            request.finished = tick;
        }
    }
}
}
