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
