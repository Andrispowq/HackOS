#include "hackos/user.h"
#include "hackos/stdio.h"

#define CHECK(test) do { if (!(test)) { printf("TCP_STREAM_FAIL %u\n", __LINE__); return 1; } } while (0)

static uint64_t Connect(unsigned port)
{
    HackosTcpStream r = {};
    r.address[0] = 10;
    r.address[2] = 2;
    r.address[3] = 2;
    r.peerPort = port;
    syscall_sleep(120);
    return syscall_tcp_stream(HACKOS_TCP_CONNECT, 0, &r);
}

static bool Wait(uint64_t handle, uint32_t events)
{
    HackosPollFd item = {handle, HACKOS_POLL_TCP | events, 0};
    return syscall_poll(&item, 1, 10000) == 1 && !(item.revents & (HACKOS_POLL_INVALID | HACKOS_POLL_ERROR));
}

int main(int argc, char** argv)
{
    HackosTcpStream r = {};
    if (argc > 1)
    {
        uint64_t parent = 0;
        for (const char* p = argv[1]; *p; ++p)
        {
            parent = parent * 10 + *p - '0';
        }
        CHECK(syscall_tcp_stream(HACKOS_TCP_STATUS, parent, &r) == HACKOS_NOT_FOUND);
        CHECK(syscall_tcp_stream(HACKOS_TCP_CLOSE, parent, nullptr) == HACKOS_NOT_FOUND);
        HackosPollFd foreign = {parent, HACKOS_POLL_TCP | HACKOS_POLL_READ, 0};
        CHECK(syscall_poll(&foreign, 1, 0) == 1 && foreign.revents == HACKOS_POLL_INVALID);
        for (unsigned i = 0; i < 4; ++i)
        {
            uint64_t child = Connect(7781);
            CHECK(child && !(child >> 63));
        }
        CHECK(Connect(7781) == HACKOS_RETRY);
        syscall_sleep(30);
        return 0; // Exit releases all owned handles and pending connections.
    }
    CHECK(syscall_tcp_stream(HACKOS_TCP_CONNECT, 0, (HackosTcpStream*)0x100000) == HACKOS_ERROR);
    CHECK(syscall_tcp_stream(HACKOS_TCP_CONNECT, 0, (HackosTcpStream*)0x4000001fffff) == HACKOS_ERROR);
    uint64_t h = Connect(7781);
    CHECK(h && !(h >> 63));
    CHECK(Wait(h, HACKOS_POLL_WRITE));
    HackosPollFd idle = {h, HACKOS_POLL_TCP | HACKOS_POLL_READ, 0};
    CHECK(syscall_poll(&idle, 1, 20) == 0);
    idle.events = HACKOS_POLL_TCP | HACKOS_POLL_WINDOW | HACKOS_POLL_READ;
    CHECK(syscall_poll(&idle, 1, 0) == HACKOS_ERROR);
    HackosPollFd mixed[2] = {{1, HACKOS_POLL_WRITE, 0}, {h, HACKOS_POLL_TCP | HACKOS_POLL_WRITE, 0}};
    CHECK(syscall_poll(mixed, 2, 0) == 2);
    CHECK(syscall_tcp_stream(HACKOS_TCP_STATUS, h, (HackosTcpStream*)0x100000) == HACKOS_ERROR);
    r.size = 513;
    CHECK(syscall_tcp_stream(HACKOS_TCP_WRITE, h, &r) == HACKOS_ERROR);
    r.size = 512;
    CHECK(syscall_tcp_stream(HACKOS_TCP_READ, h, &r) == HACKOS_RETRY);
    static const HackosTcpStream readOnly = {{}, 0, 0, 512};
    for (unsigned chunk = 0; chunk < 12; ++chunk)
    {
        CHECK(Wait(h, HACKOS_POLL_WRITE));
        r.size = 512;
        for (unsigned i = 0; i < 512; ++i)
        {
            r.data[i] = (uint8_t)(i + chunk);
        }
        CHECK(syscall_tcp_stream(HACKOS_TCP_WRITE, h, &r) == 0);
        CHECK(Wait(h, HACKOS_POLL_READ));
        CHECK(syscall_tcp_stream(HACKOS_TCP_READ, h, (HackosTcpStream*)&readOnly) == HACKOS_ERROR);
        unsigned received = 0;
        while (received < 512)
        {
            CHECK(Wait(h, HACKOS_POLL_READ));
            r.size = 17;
            CHECK(syscall_tcp_stream(HACKOS_TCP_READ, h, &r) == 0 && r.size && r.size <= 17);
            for (unsigned i = 0; i < r.size; ++i)
            {
                CHECK(r.data[i] == (uint8_t)(received + i + chunk));
            }
            received += r.size;
        }
    }
    CHECK(syscall_tcp_stream(HACKOS_TCP_STATUS, h, &r) == 0 && r.sent == 6144 && r.received == 6144);
    char handleText[32];
    snprintf(handleText, sizeof(handleText), "%llu", (unsigned long long)h);
    for (unsigned i = 0; i < 3; ++i)
    {
        const char* args[] = {"tcpstreamprobe", handleText};
        uint64_t child = syscall_spawn_args("/usr/bin/tcpstreamprobe", 2, args);
        int64_t status = -1;
        CHECK(!(child >> 63) && syscall_wait(child, &status, 0) == child && status == 0);
    }
    CHECK(syscall_tcp_stream(HACKOS_TCP_SHUTDOWN_WRITE, h, nullptr) == 0);
    r.size = 1;
    CHECK(syscall_tcp_stream(HACKOS_TCP_WRITE, h, &r) == HACKOS_ERROR);
    CHECK(Wait(h, HACKOS_POLL_READ));
    r.size = 512;
    CHECK(syscall_tcp_stream(HACKOS_TCP_READ, h, &r) == 0 && r.size == 0 && (r.flags & HACKOS_TCP_EOF));
    CHECK(syscall_tcp_stream(HACKOS_TCP_CLOSE, h, nullptr) == 0);
    CHECK(syscall_tcp_stream(HACKOS_TCP_STATUS, h, &r) == HACKOS_NOT_FOUND);
    h = Connect(7783);
    CHECK(h && !(h >> 63));
    CHECK(Wait(h, HACKOS_POLL_READ));
    syscall_sleep(500);
    CHECK(syscall_tcp_stream(HACKOS_TCP_STATUS, h, &r) == 0 && r.queued == 2048);
    unsigned received = 0;
    while (received < 8192)
    {
        CHECK(Wait(h, HACKOS_POLL_READ));
        r.size = 127;
        CHECK(syscall_tcp_stream(HACKOS_TCP_READ, h, &r) == 0 && r.size);
        for (unsigned i = 0; i < r.size; ++i)
        {
            CHECK(r.data[i] == (uint8_t)(received + i));
        }
        received += r.size;
    }
    CHECK(Wait(h, HACKOS_POLL_READ));
    r.size = 512;
    CHECK(syscall_tcp_stream(HACKOS_TCP_READ, h, &r) == 0 && !r.size && (r.flags & HACKOS_TCP_EOF));
    CHECK(syscall_tcp_stream(HACKOS_TCP_SHUTDOWN_WRITE, h, nullptr) == 0);
    for (unsigned i = 0; i < 100; ++i)
    {
        CHECK(syscall_tcp_stream(HACKOS_TCP_STATUS, h, &r) == 0);
        if (r.state != HACKOS_TCP_PENDING) break;
        syscall_sleep(20);
    }
    CHECK(r.state == HACKOS_TCP_COMPLETE && r.received == 8192);
    CHECK(syscall_tcp_stream(HACKOS_TCP_CLOSE, h, nullptr) == 0);
    User::Print("TCP_STREAM_OK\n");
    return 0;
}
