#pragma once
#include "resolve.h"
#include "hackos/runtime/ipv4.h"

namespace TcpDemo
{
struct Socket
{
    uint64_t handle;
    ~Socket()
    {
        syscall_tcp_stream(HACKOS_TCP_CLOSE, handle, nullptr);
    }
};

inline bool Wait(uint64_t handle, uint32_t events)
{
    HackosPollFd item = {handle, HACKOS_POLL_TCP | events, 0};
    uint64_t ready = syscall_poll(&item, 1, 20000);
    return ready == 1 && !(item.revents & (HACKOS_POLL_ERROR | HACKOS_POLL_INVALID));
}

inline unsigned Number(const char* text, unsigned limit)
{
    unsigned value = 0;
    for (const char* p = text; *p; ++p)
    {
        if (*p < '0' || *p > '9' || value > limit / 10) return 0;
        value = value * 10 + *p - '0';
        if (value > limit) return 0;
    }
    return value;
}

inline int Run(int argc, char** argv)
{
    const char* usage = "Usage: tcp --stream <host> <port> <count:1..1024> <message>\n"
                        "Repeat a 1..512-byte message on one connection, verifying each echo before the next write.\n"
                        "Demo: tcp --stream 10.0.2.2 7779 256 \"hello over TCP\"\n";
    if (argc != 6)
    {
        Error(usage);
        return 2;
    }
    unsigned port = Number(argv[3], 65535), count = Number(argv[4], 1024), size = Length(argv[5]);
    if (!port || !count || !size || size > 512)
    {
        Error(usage);
        return 2;
    }
    HackosTcpStream record = {};
    if (!Hackos::ParseIPv4(argv[2], record.address))
    {
        HackosResolve result = {};
        if (!ResolveHost(argv[2], result)) return 1;
        for (unsigned i = 0; i < 4; ++i)
        {
            record.address[i] = result.address[i];
        }
    }
    record.peerPort = port;
    uint64_t handle = syscall_tcp_stream(HACKOS_TCP_CONNECT, 0, &record);
    if (!handle || (handle >> 63))
    {
        Error("tcp: cannot open stream (invalid destination or slots busy).\n");
        return 1;
    }
    Socket socket = {handle};
    if (!Wait(handle, HACKOS_POLL_WRITE))
    {
        Error("tcp: stream connection failed or timed out.\n");
        return 1;
    }
    for (unsigned iteration = 0; iteration < count; ++iteration)
    {
        if (!Wait(handle, HACKOS_POLL_WRITE))
        {
            Error("tcp: stream write wait failed.\n");
            return 1;
        }
        record.size = size;
        for (unsigned i = 0; i < size; ++i)
        {
            record.data[i] = argv[5][i];
        }
        if (syscall_tcp_stream(HACKOS_TCP_WRITE, handle, &record))
        {
            Error("tcp: stream write failed.\n");
            return 1;
        }
        unsigned received = 0;
        while (received < size)
        {
            if (!Wait(handle, HACKOS_POLL_READ))
            {
                Error("tcp: stream read wait failed.\n");
                return 1;
            }
            record.size = size - received;
            if (syscall_tcp_stream(HACKOS_TCP_READ, handle, &record) || !record.size)
            {
                Error("tcp: stream ended before the echo completed.\n");
                return 1;
            }
            for (unsigned i = 0; i < record.size; ++i)
            {
                if (record.data[i] != (uint8_t)argv[5][received + i])
                {
                    Error("tcp: stream echo did not match.\n");
                    return 1;
                }
            }
            received += record.size;
        }
    }
    if (syscall_tcp_stream(HACKOS_TCP_SHUTDOWN_WRITE, handle, nullptr))
    {
        Error("tcp: stream shutdown failed.\n");
        return 1;
    }
    if (!Wait(handle, HACKOS_POLL_READ))
    {
        Error("tcp: stream EOF wait failed.\n");
        return 1;
    }
    record.size = 512;
    if (syscall_tcp_stream(HACKOS_TCP_READ, handle, &record) || record.size || !(record.flags & HACKOS_TCP_EOF))
    {
        Error("tcp: expected EOF after the final echo.\n");
        return 1;
    }
    // The final ACK is queued by idle after EOF becomes visible.
    for (unsigned wait = 0; wait < 100; ++wait)
    {
        if (syscall_tcp_stream(HACKOS_TCP_STATUS, handle, &record)) return 1;
        if (record.state != HACKOS_TCP_PENDING) break;
        syscall_sleep(20);
    }
    if (record.state != HACKOS_TCP_COMPLETE)
    {
        Error("tcp: stream close did not complete.\n");
        return 1;
    }
    ReportOutput out;
    out.Format("TCP stream complete: %u chunks, sent %u bytes, received %u bytes on one connection\n",
               count, (unsigned)record.sent, (unsigned)record.received);
    out.Format("Verified every echo; local port %u, retransmissions %u\n", record.localPort, record.retransmissions);
    return out.ok ? 0 : 1;
}
}
