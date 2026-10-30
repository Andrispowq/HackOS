#include "ping.h"
#include "icmp.h"
#include "arp.h"
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
    HackosEcho result;
    uint8_t data[EchoDataSize];
};
Request requests[8];
uint64_t nextToken, lastSend;
bool hasSent;
const uint8_t gateway[4] = {10, 0, 2, 2};

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
    if (operation != HACKOS_ECHO_START || !EqualBytes(record.address, gateway, 4)) return HACKOS_ERROR;
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
        found->result.address[i] = gateway[i];
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
    if (!ParseEchoReply(frame, length, mac, ip, packet)) return;
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

void PollEcho(bool ready, bool link, bool resolved, const uint8_t* mac, const uint8_t* peerMac,
              const uint8_t* ip, const uint8_t* peerIp)
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
        // Global 10 packets/second ceiling, including cancellation/restart loops.
        if (!resolved || (hasSent && tick - lastSend < TimerTicksForMilliseconds(100))) continue;
        uint8_t frame[EchoFrameSize];
        BuildEcho(frame, mac, peerMac, ip, peerIp, (uint16_t)(request.token >> 16),
                  (uint16_t)request.token, request.data);
        if (Transmit(frame, sizeof(frame)))
        {
            request.sent = true;
            request.sentAt = lastSend = tick;
            hasSent = true;
        }
        break;
    }
}
}
