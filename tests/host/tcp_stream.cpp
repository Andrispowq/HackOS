#include "net/tcp.h"
#include "net/route.h"
#include "hackos/abi.h"
#include <cassert>

extern volatile uint64_t tick;
extern unsigned sends, transmittedLength;
extern uint8_t transmitted[1514];

void TestTcpStream()
{
    using namespace Net;
    const uint8_t mac[6] = {0x52,0x54,0,0x12,0x34,0x56}, peer[6] = {0x52,0x55,10,0,2,2};
    const uint8_t ip[4] = {10,0,2,15}, gateway[4] = {10,0,2,2}, mask[4] = {255,255,255,0}, dns[4] = {10,0,2,3};
    ConfigureNetwork(ip, mask, gateway, dns);
    uint8_t frame[1514], hardware[6];
    ResetNeighbors();
    ResolveNeighbor(gateway, hardware);
    PollNeighbors(mac);
    BuildArp(frame, 2, peer, gateway, mac, ip);
    ArpPacket arp;
    assert(ParseArp(frame, 60, mac, arp));
    ReceiveNeighbor(frame, arp, mac);
    HackosTcpStream r = {};
    __builtin_memcpy(r.address, gateway, 4);
    r.peerPort = 7779;
    tick += 100;
    uint64_t handle = TcpStreamControl(101, HACKOS_TCP_CONNECT, 0, r);
    assert(handle && !(handle >> 63));
    assert(TcpStreamControl(102, HACKOS_TCP_STATUS, handle, r) == HACKOS_NOT_FOUND);
    assert(TcpReadiness(102, handle, HACKOS_POLL_READ) == HACKOS_POLL_INVALID);
    assert(!TcpReadiness(101, handle, HACKOS_POLL_WRITE));
    TcpPacket p;
    auto poll = [&]()
    {
        tick += 2;
        PollTcp(true, true, mac, ip);
        assert(ParseTcp(transmitted, transmittedLength, peer, gateway, p));
    };
    poll();
    assert(p.flags == TcpSyn);
    uint32_t tx = p.sequence + 1, rx = 0xffffff00;
    uint16_t port = p.sourcePort;
    auto reply = [&](uint8_t flags, const uint8_t* data, unsigned size, uint16_t window = 2048, uint32_t sequence = 0)
    {
        unsigned n = BuildTcp(frame, peer, mac, gateway, ip, 7779, port,
                              sequence ? sequence : rx, tx, flags, window, data, size);
        ReceiveTcp(frame, n, mac, ip);
    };
    reply(TcpSyn | TcpAck, nullptr, 0, 2048, rx - 1);
    poll();
    assert(p.flags == TcpAck && p.acknowledgement == rx);
    unsigned count = sends;
    poll();
    assert(sends == count); // No automatic FIN for an idle stream.
    tick += 3000;
    assert(TcpStreamControl(101, HACKOS_TCP_STATUS, handle, r) == 0 && (r.flags & HACKOS_TCP_WRITABLE));
    HackosTcp legacy = {};
    assert(TcpControl(101, HACKOS_TCP_QUERY, legacy) == HACKOS_NOT_FOUND);
    assert(TcpControl(101, HACKOS_TCP_CANCEL, legacy) == 0);
    assert(TcpStreamControl(101, HACKOS_TCP_STATUS, handle, r) == 0);
    uint8_t payload[512];
    for (unsigned i = 0; i < 512; ++i)
    {
        payload[i] = i;
    }
    for (unsigned iteration = 0; iteration < 12; ++iteration)
    {
        r.size = 512;
        __builtin_memcpy(r.data, payload, 512);
        assert(TcpStreamControl(101, HACKOS_TCP_WRITE, handle, r) == 0);
        assert(TcpStreamControl(101, HACKOS_TCP_WRITE, handle, r) == HACKOS_RETRY);
        poll(); // A window-update ACK may precede the next data segment.
        if (!p.size) poll();
        assert(p.size == 512 && p.sequence == tx && p.sourcePort == port);
        tx += 512;
        reply(TcpAck | TcpPush, payload, 512);
        rx += 512;
        assert(TcpReadiness(101, handle, HACKOS_POLL_READ | HACKOS_POLL_WRITE) == (HACKOS_POLL_READ | HACKOS_POLL_WRITE));
        r.size = 7;
        assert(TcpStreamControl(101, HACKOS_TCP_READ, handle, r) == 0 && r.size == 7 && r.data[6] == 6);
        r.size = 7;
        assert(TcpStreamControl(101, HACKOS_TCP_READ, handle, r) == 0 && r.data[0] == 0); // Peek is non-destructive.
        ConsumeTcp(101, handle, 7);
        r.size = 512;
        assert(TcpStreamControl(101, HACKOS_TCP_READ, handle, r) == 0 && r.size == 505 && r.data[0] == 7);
        ConsumeTcp(101, handle, r.size);
    }
    assert(TcpStreamControl(101, HACKOS_TCP_STATUS, handle, r) == 0 && r.sent == 6144 && r.received == 6144 && r.queued == 0);
    // Fill receive storage; excess bytes are not acknowledged and can be retried.
    for (unsigned i = 0; i < 4; ++i)
    {
        reply(TcpAck, payload, 512);
        rx += 512;
    }
    reply(TcpAck, payload, 512);
    poll();
    assert(p.window == 0 && p.acknowledgement == rx);
    r.size = 512;
    assert(TcpStreamControl(101, HACKOS_TCP_READ, handle, r) == 0 && r.size == 512);
    ConsumeTcp(101, handle, 512);
    poll();
    assert(p.window == 512 && p.acknowledgement == rx);
    reply(TcpAck, payload, 512);
    rx += 512;
    for (unsigned i = 0; i < 4; ++i)
    {
        r.size = 512;
        assert(TcpStreamControl(101, HACKOS_TCP_READ, handle, r) == 0 && r.size == 512);
        ConsumeTcp(101, handle, 512);
    }
    // Lost window updates are solicited by bounded zero-window probes.
    reply(TcpAck, nullptr, 0, 0);
    r.size = 1;
    r.data[0] = 'z';
    assert(TcpStreamControl(101, HACKOS_TCP_WRITE, handle, r) == 0);
    poll(); // Flush receive-window ACK.
    tick += 100;
    poll();
    assert(p.size == 0 && p.sequence == tx - 1);
    reply(TcpAck, nullptr, 0, 512);
    poll();
    assert(p.size == 1 && p.sequence == tx);
    ++tx;
    reply(TcpAck, nullptr, 0);
    // Peer EOF does not prevent further local writes until explicit shutdown.
    reply(TcpAck | TcpFin, nullptr, 0);
    ++rx;
    r.size = 512;
    assert(TcpStreamControl(101, HACKOS_TCP_READ, handle, r) == 0 && !r.size && (r.flags & HACKOS_TCP_EOF));
    assert(TcpReadiness(101, handle, HACKOS_POLL_READ) == (HACKOS_POLL_READ | HACKOS_POLL_HANGUP));
    r.size = 1;
    r.data[0] = 'q';
    assert(TcpStreamControl(101, HACKOS_TCP_WRITE, handle, r) == 0);
    poll();
    poll();
    assert(p.size == 1 && p.data[0] == 'q');
    ++tx;
    reply(TcpAck, nullptr, 0);
    assert(TcpStreamControl(101, HACKOS_TCP_SHUTDOWN_WRITE, handle, r) == 0);
    r.size = 1;
    assert(TcpStreamControl(101, HACKOS_TCP_WRITE, handle, r) == HACKOS_ERROR);
    poll();
    poll();
    assert(p.flags == (TcpFin | TcpAck) && p.sequence == tx);
    ++tx;
    reply(TcpAck, nullptr, 0);
    poll();
    assert(TcpStreamControl(101, HACKOS_TCP_STATUS, handle, r) == 0 && r.state == HACKOS_TCP_COMPLETE);
    tick += 13000;
    poll();
    assert(TcpStreamControl(101, HACKOS_TCP_STATUS, handle, r) == 0); // Handle outlives TIME-WAIT.
    assert(TcpStreamControl(101, HACKOS_TCP_CLOSE, handle, r) == 0);
    assert(TcpReadiness(101, handle, 0) == HACKOS_POLL_INVALID);
    r = {};
    __builtin_memcpy(r.address, gateway, 4);
    r.peerPort = 7779;
    uint64_t handles[4];
    for (unsigned i = 0; i < 4; ++i)
    {
        tick += 100;
        handles[i] = TcpStreamControl(101, HACKOS_TCP_CONNECT, 0, r);
        assert(handles[i] > handle && !(handles[i] >> 63));
    }
    tick += 100;
    assert(TcpStreamControl(101, HACKOS_TCP_CONNECT, 0, r) == HACKOS_RETRY);
    InvalidateTcp();
    assert(TcpReadiness(101, handles[0], HACKOS_POLL_READ) & HACKOS_POLL_ERROR);
    CancelTcp(101);
    assert(TcpStreamControl(101, HACKOS_TCP_STATUS, handles[0], r) == HACKOS_NOT_FOUND);
}
