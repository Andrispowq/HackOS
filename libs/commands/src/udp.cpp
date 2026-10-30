#include "resolve.h"
#include "hackos/runtime/ipv4.h"

struct Endpoint
{
    uint64_t handle;
    ~Endpoint()
    {
        syscall_udp(HACKOS_UDP_CLOSE, handle, nullptr);
    }
};

int main(int argc, char** argv)
{
    const char* usage = "Usage: udp <IPv4-address|hostname> <port> <message>\n"
                        "Send up to 512 bytes and wait for one reply. Quote messages containing spaces.\n"
                        "Compose demo: udp 10.0.2.2 7777 \"hello from HackOS\"\n";
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
    uint64_t size = Length(argv[3]);
    if (!port || port > 65535 || size > 512)
    {
        Error(usage);
        return 2;
    }
    HackosDatagram record = {};
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
    uint64_t handle = syscall_udp(HACKOS_UDP_OPEN, 0, &record);
    if (!handle || (handle >> 63))
    {
        Error(handle == HACKOS_RETRY ? "udp: sockets or ports busy.\n" : "udp: unsupported destination.\n");
        return 1;
    }
    Endpoint endpoint = {handle};
    record.size = size;
    for (unsigned i = 0; i < size; ++i)
    {
        record.data[i] = argv[3][i];
    }
    if (syscall_udp(HACKOS_UDP_SEND, handle, &record))
    {
        Error("udp: could not queue datagram.\n");
        return 1;
    }
    for (unsigned wait = 0; wait < 200; ++wait)
    {
        if (syscall_udp(HACKOS_UDP_STATUS, handle, &record))
        {
            Error("udp: socket unavailable.\n");
            return 1;
        }
        if (record.state != HACKOS_UDP_PENDING) break;
        syscall_sleep(20);
    }
    if (record.state != HACKOS_UDP_SENT)
    {
        const char* error = "udp: send timed out.\n";
        if (record.state == HACKOS_UDP_UNAVAILABLE) error = "udp: network unavailable.\n";
        if (record.state == HACKOS_UDP_NEIGHBOR_FAILED) error = "udp: next-hop ARP resolution failed.\n";
        Error(error);
        return 1;
    }
    ReportOutput out;
    out.Format("Sent %u bytes to %u.%u.%u.%u:%u (local port %u)\n", (unsigned)size,
               record.address[0], record.address[1], record.address[2], record.address[3], record.peerPort, record.localPort);
    if (!out.ok) return 1;
    for (unsigned wait = 0; wait < 150; ++wait)
    {
        uint64_t status = syscall_udp(HACKOS_UDP_RECEIVE, handle, &record);
        if (!status)
        {
            out.Format("Received %u bytes: ", record.size);
            // Display remote control/binary bytes literally, never as terminal escapes.
            char text[2049];
            unsigned used = 0;
            const char* digits = "0123456789abcdef";
            for (unsigned i = 0; i < record.size; ++i)
            {
                uint8_t c = record.data[i];
                if (c >= 32 && c <= 126 && c != '\\')
                {
                    text[used++] = c;
                }
                else
                {
                    text[used++] = '\\';
                    text[used++] = 'x';
                    text[used++] = digits[c >> 4];
                    text[used++] = digits[c & 15];
                }
            }
            text[used] = 0;
            out.Text(text);
            out.Text("\n");
            return out.ok ? 0 : 1;
        }
        if (status != HACKOS_RETRY)
        {
            Error("udp: receive failed.\n");
            return 1;
        }
        syscall_sleep(20);
    }
    Error("udp: receive timed out (delivery is not guaranteed).\n");
    return 1;
}
