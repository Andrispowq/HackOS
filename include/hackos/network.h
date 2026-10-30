#pragma once
#include "hackos/types.h"

enum HackosEchoOperation : uint64_t
{
    HACKOS_ECHO_START,
    HACKOS_ECHO_QUERY,
    HACKOS_ECHO_CANCEL
};

enum HackosEchoState : uint32_t
{
    HACKOS_ECHO_PENDING,
    HACKOS_ECHO_REPLY,
    HACKOS_ECHO_TIMEOUT,
    HACKOS_ECHO_UNAVAILABLE,
    HACKOS_ECHO_UNREACHABLE,
    HACKOS_ECHO_TIME_EXCEEDED,
    HACKOS_ECHO_NEIGHBOR_FAILED
};

// START copies address only; QUERY copies the entire record. CANCEL ignores it.
// One request per process, eight globally. Static connected /24 plus default route.
struct HackosEcho
{
    uint8_t address[4];
    uint32_t state;
    uint64_t elapsedMs;
    uint32_t sequence;
    union
    {
        uint32_t ttl; // Echo reply TTL.
        uint32_t errorCode; // ICMP code for UNREACHABLE / TIME_EXCEEDED.
    };
};
static_assert(sizeof(HackosEcho) == 24, "echo ABI");
static_assert(__builtin_offsetof(HackosEcho, elapsedMs) == 8, "echo timing ABI");

enum HackosResolveOperation : uint64_t
{
    HACKOS_RESOLVE_START,
    HACKOS_RESOLVE_QUERY,
    HACKOS_RESOLVE_CANCEL
};
enum HackosResolveState : uint32_t
{
    HACKOS_RESOLVE_PENDING,
    HACKOS_RESOLVE_REPLY,
    HACKOS_RESOLVE_TIMEOUT,
    HACKOS_RESOLVE_UNAVAILABLE,
    HACKOS_RESOLVE_NEIGHBOR_FAILED,
    HACKOS_RESOLVE_NOT_FOUND,
    HACKOS_RESOLVE_SERVER_ERROR,
    HACKOS_RESOLVE_TRUNCATED,
    HACKOS_RESOLVE_NO_ADDRESS
};
struct HackosResolve
{
    char name[256];
    uint32_t state;
    uint8_t address[4];
    uint32_t ttl;
    uint32_t errorCode;
};
static_assert(sizeof(HackosResolve) == 272, "resolver ABI");
static_assert(__builtin_offsetof(HackosResolve, state) == 256, "resolver state ABI");

// Connected IPv4 UDP endpoints, independent of stream descriptors.
enum HackosUdpOperation : uint64_t
{
    HACKOS_UDP_OPEN,
    HACKOS_UDP_SEND,
    HACKOS_UDP_RECEIVE,
    HACKOS_UDP_STATUS,
    HACKOS_UDP_CLOSE
};
enum HackosUdpState : uint32_t
{
    HACKOS_UDP_IDLE,
    HACKOS_UDP_PENDING,
    HACKOS_UDP_SENT,
    HACKOS_UDP_TIMEOUT,
    HACKOS_UDP_UNAVAILABLE,
    HACKOS_UDP_NEIGHBOR_FAILED
};
struct HackosDatagram
{
    uint8_t address[4];
    uint16_t peerPort, localPort;
    uint32_t size, state, queued, dropped;
    uint64_t sent;
    uint8_t data[512];
};
static_assert(sizeof(HackosDatagram) == 544, "UDP ABI");
static_assert(__builtin_offsetof(HackosDatagram, data) == 32, "UDP payload ABI");
