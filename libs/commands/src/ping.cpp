#include "output.h"

int main(int argc, char** argv)
{
    const char* usage = "Usage: ping [-c 1..10] 10.0.2.2\n"
                        "IPv4/ICMP gateway demo. Default: 4 probes, 32 data bytes, 3-second timeout.\n";
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
    if (!target || !Equal(target, "10.0.2.2") || !count || count > 10)
    {
        Error(usage);
        return 2;
    }
    ReportOutput out;
    out.Text("PING 10.0.2.2: 32 data bytes\n");
    unsigned received = 0, attempted = 0;
    for (unsigned i = 0; i < count && out.ok; ++i)
    {
        HackosEcho request = {{10, 0, 2, 2}, 0, 0, 0, 0};
        uint64_t status = syscall_echo(HACKOS_ECHO_START, &request);
        if (status)
        {
            Error(status == HACKOS_RETRY ? "ping: request slots busy.\n" : "ping: request failed.\n");
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
            out.Format("32 bytes from 10.0.2.2: seq=%u ttl=%u time=%lu ms\n", i + 1, request.ttl, request.elapsedMs);
        }
        else if (request.state == HACKOS_ECHO_UNAVAILABLE)
        {
            out.Text("ping: network unavailable (no supported NIC or link down).\n");
            break;
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
