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
    HACKOS_ECHO_UNAVAILABLE
};

// START copies address only; QUERY copies the entire record. CANCEL ignores it.
// One request per process, eight globally. First version targets 10.0.2.2 only.
struct HackosEcho
{
    uint8_t address[4];
    uint32_t state;
    uint64_t elapsedMs;
    uint32_t sequence, ttl;
};
static_assert(sizeof(HackosEcho) == 24, "echo ABI");
static_assert(__builtin_offsetof(HackosEcho, elapsedMs) == 8, "echo timing ABI");
