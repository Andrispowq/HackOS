#pragma once
#include "output.h"
#include "hackos/runtime/hostname.h"

inline bool ResolveHost(const char* name, HackosResolve& result)
{
    if (!Hackos::NormalizeHostname(name, result.name))
    {
        Error("DNS: invalid hostname.\n");
        return false;
    }
    uint64_t status = syscall_resolve(HACKOS_RESOLVE_START, &result);
    if (status)
    {
        Error(status == HACKOS_RETRY ? "DNS: lookup slots busy.\n" : "DNS: cannot start lookup.\n");
        return false;
    }
    do
    {
        syscall_sleep(20);
        status = syscall_resolve(HACKOS_RESOLVE_QUERY, &result);
    } while (!status && result.state == HACKOS_RESOLVE_PENDING);
    syscall_resolve(HACKOS_RESOLVE_CANCEL, nullptr);
    if (!status && result.state == HACKOS_RESOLVE_REPLY) return true;
    const char* message = "DNS: lookup failed.\n";
    if (!status)
    {
        switch (result.state)
        {
        case HACKOS_RESOLVE_TIMEOUT:
            message = "DNS: lookup timed out.\n";
            break;
        case HACKOS_RESOLVE_UNAVAILABLE:
            message = "DNS: network unavailable.\n";
            break;
        case HACKOS_RESOLVE_NEIGHBOR_FAILED:
            message = "DNS: next-hop ARP resolution failed.\n";
            break;
        case HACKOS_RESOLVE_NOT_FOUND:
            message = "DNS: name not found (NXDOMAIN).\n";
            break;
        case HACKOS_RESOLVE_SERVER_ERROR:
            message = "DNS: server returned an error.\n";
            break;
        case HACKOS_RESOLVE_TRUNCATED:
            message = "DNS: response truncated; TCP fallback is not available yet.\n";
            break;
        case HACKOS_RESOLVE_NO_ADDRESS:
            message = "DNS: no supported IPv4 answer.\n";
            break;
        }
    }
    Error(message);
    return false;
}
