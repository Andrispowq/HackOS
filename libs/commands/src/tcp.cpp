#include "resolve.h"
#include "hackos/runtime/ipv4.h"

int main(int argc, char** argv)
{
    const char* usage = "Usage: tcp <IPv4-address|hostname> <port> <message>\n"
                        "Send up to 512 bytes, half-close, and collect up to 2048 reply bytes through EOF.\n"
                        "Demo: tcp 10.0.2.2 7778 \"hello over TCP\"\n";
    if (argc == 2 && (Equal(argv[1], "--help") || Equal(argv[1], "-h")))
    {
        Print(usage);
        return 0;
    }
    if (argc != 4)
    {
        Error(usage);
        return 2;
    }
    unsigned port = 0;
    for (const char* p = argv[2]; *p; ++p)
    {
        if (*p < '0' || *p > '9' || port > 6553)
        {
            Error(usage);
            return 2;
        }
        port = port * 10 + (*p - '0');
    }
    unsigned size = Length(argv[3]);
    if (!port || port > 65535 || size > 512)
    {
        Error(usage);
        return 2;
    }
    HackosTcp record = {};
    if (!Hackos::ParseIPv4(argv[1], record.address))
    {
        HackosResolve result = {};
        if (!ResolveHost(argv[1], result)) return 1;
        for (unsigned i = 0; i < 4; ++i)
        {
            record.address[i] = result.address[i];
        }
    }
    record.peerPort = port;
    record.size = size;
    for (unsigned i = 0; i < size; ++i)
    {
        record.data[i] = argv[3][i];
    }
    uint64_t status = syscall_tcp(HACKOS_TCP_START, &record);
    if (status)
    {
        Error(status == HACKOS_RETRY ? "tcp: connection slots busy; try again.\n" : "tcp: unsupported request.\n");
        return 1;
    }
    do
    {
        syscall_sleep(20);
        status = syscall_tcp(HACKOS_TCP_QUERY, &record);
    } while (!status && record.state == HACKOS_TCP_PENDING);
    syscall_tcp(HACKOS_TCP_CANCEL, nullptr);
    if (status || record.state != HACKOS_TCP_COMPLETE)
    {
        const char* error = "tcp: exchange failed.\n";
        if (!status)
        {
            switch (record.state)
            {
            case HACKOS_TCP_TIMEOUT:
                error = "tcp: exchange timed out.\n";
                break;
            case HACKOS_TCP_UNAVAILABLE:
                error = "tcp: network unavailable.\n";
                break;
            case HACKOS_TCP_NEIGHBOR_FAILED:
                error = "tcp: next-hop ARP resolution failed.\n";
                break;
            case HACKOS_TCP_RESET:
                error = "tcp: connection refused or reset.\n";
                break;
            case HACKOS_TCP_TOO_LARGE:
                error = "tcp: response exceeds 2048 bytes.\n";
                break;
            }
        }
        Error(error);
        return 1;
    }
    ReportOutput out;
    out.Format("TCP exchange complete: sent %u bytes, received %u bytes, retransmissions %u\n",
               record.size, record.received, record.retransmissions);
    out.Text("Reply: ");
    // Escape peer-controlled bytes so the network cannot inject terminal commands.
    const char* digits = "0123456789abcdef";
    for (unsigned i = 0; i < record.received; ++i)
    {
        uint8_t c = record.data[i];
        char text[5] = {};
        if (c >= 32 && c <= 126 && c != '\\')
        {
            text[0] = c;
        }
        else
        {
            text[0] = '\\';
            text[1] = 'x';
            text[2] = digits[c >> 4];
            text[3] = digits[c & 15];
        }
        out.Text(text);
    }
    out.Text("\n");
    return out.ok ? 0 : 1;
}
