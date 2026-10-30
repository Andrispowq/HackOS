#include "resolve.h"
#include "hackos/runtime/ipv4.h"

int main(int argc, char** argv)
{
    const char* usage = "Usage: ping [-c 1..10] <IPv4-address|hostname>\n"
                        "IPv4/ICMP with static 10.0.2.0/24 and default gateway 10.0.2.2.\n"
                        "Default: 4 probes, 32 data bytes, 3-second timeout.\n";
    unsigned count = 4;
    const char* target = argc == 2 ? argv[1] : nullptr;
    if (argc == 4 && Equal(argv[1], "-c"))
    {
        target = argv[3];
        count = 0;
        const char* number = argv[2];
        if (!*number) target = nullptr;
        for (; *number; ++number)
        {
            if (*number < '0' || *number > '9' || count > 10)
            {
                target = nullptr;
                break;
            }
            count = count * 10 + (*number - '0');
        }
    }
    if (argc == 2 && (Equal(argv[1], "--help") || Equal(argv[1], "-h")))
    {
        Print(usage);
        return 0;
    }
    uint8_t address[4];
    char hostname[256];
    bool numeric = target && Hackos::ParseIPv4(target, address);
    if (!target || (!numeric && !Hackos::NormalizeHostname(target, hostname)) || !count || count > 10)
    {
        Error(usage);
        return 2;
    }
    char resolved[16];
    if (!numeric)
    {
        HackosResolve result = {};
        if (!ResolveHost(hostname, result)) return 1;
        for (unsigned i = 0; i < 4; ++i)
        {
            address[i] = result.address[i];
        }
        snprintf(resolved, sizeof(resolved), "%u.%u.%u.%u", address[0], address[1], address[2], address[3]);
        ReportOutput resolution;
        resolution.Format("Resolved %s to %s\n", hostname, resolved);
        if (!resolution.ok) return 1;
        target = resolved;
    }
    ReportOutput out;
    out.Format("PING %s: 32 data bytes\n", target);
    unsigned received = 0, attempted = 0;
    for (unsigned i = 0; i < count && out.ok; ++i)
    {
        HackosEcho request = {};
        for (unsigned j = 0; j < 4; ++j)
        {
            request.address[j] = address[j];
        }
        uint64_t status = syscall_echo(HACKOS_ECHO_START, &request);
        if (status)
        {
            Error(status == HACKOS_RETRY ? "ping: request slots busy.\n" : "ping: unsupported address or request failed.\n");
            return 1;
        }
        ++attempted;
        do
        {
            syscall_sleep(20);
            status = syscall_echo(HACKOS_ECHO_QUERY, &request);
        } while (!status && request.state == HACKOS_ECHO_PENDING);
        syscall_echo(HACKOS_ECHO_CANCEL, nullptr);
        if (status)
        {
            Error("ping: result unavailable.\n");
            return 1;
        }
        if (request.state == HACKOS_ECHO_REPLY)
        {
            ++received;
            out.Format("32 bytes from %s: seq=%u ttl=%u time=%lu ms\n", target, i + 1, request.ttl, request.elapsedMs);
        }
        else if (request.state == HACKOS_ECHO_UNAVAILABLE)
        {
            out.Text("ping: network unavailable (no supported NIC or link down).\n");
            break;
        }
        else if (request.state == HACKOS_ECHO_NEIGHBOR_FAILED)
        {
            out.Format("Next-hop ARP resolution failed: seq=%u\n", i + 1);
        }
        else if (request.state == HACKOS_ECHO_UNREACHABLE || request.state == HACKOS_ECHO_TIME_EXCEEDED)
        {
            out.Format("%s: seq=%u ICMP code=%u\n",
                       request.state == HACKOS_ECHO_UNREACHABLE ? "Destination unreachable" : "Time exceeded",
                       i + 1, request.errorCode);
        }
        else
        {
            out.Format("Request timeout: seq=%u\n", i + 1);
        }
        if (i + 1 < count && out.ok) syscall_sleep(1000);
    }
    out.Format("%u probes, %u received, %u%% loss\n", attempted, received,
               attempted ? (attempted - received) * 100 / attempted : 0);
    return out.ok && received == attempted && attempted ? 0 : 1;
}
