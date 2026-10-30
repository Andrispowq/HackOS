#include "net/tcp.h"
#include "net/route.h"
#include "net/icmp.h"
#include "hackos/abi.h"
#include <cassert>

extern volatile uint64_t tick;
extern unsigned sends, transmittedLength;
extern uint8_t transmitted[1514];

void TestTcp()
{
    using namespace Net;
    const uint8_t mac[6] = {0x52,0x54,0,0x12,0x34,0x56}, peer[6] = {0x52,0x55,10,0,2,2};
    const uint8_t ip[4] = {10,0,2,15}, gateway[4] = {10,0,2,2}, mask[4] = {255,255,255,0}, dns[4] = {10,0,2,3};
    ConfigureNetwork(ip, mask, gateway, dns);
    uint8_t frame[1514];
    unsigned length = BuildTcp(frame, peer, mac, gateway, ip, 7778, 50000, 0xfffffffd, 42, TcpSyn | TcpAck, 2048, nullptr, 0);
    TcpPacket p;
    assert(ParseTcp(frame, length, mac, ip, p) && p.mss == 512 && p.sequence == 0xfffffffd);
    for (unsigned size = 0; size < 58; ++size)
    {
        auto* shortFrame = new uint8_t[size];
        __builtin_memcpy(shortFrame, frame, size);
        assert(!ParseTcp(shortFrame, size, mac, ip, p));
        delete[] shortFrame;
    }
    for (unsigned at = 0; at < 58; ++at)
    {
        uint8_t bad[1514];
        __builtin_memcpy(bad, frame, length);
        bad[at] ^= 0x40;
        // Source MAC is not authenticated; every IP/TCP mutation must fail checksum.
        if (at < 6 || at >= 12) assert(!ParseTcp(bad, length, mac, ip, p));
    }
    assert(!ParseTcp(nullptr, 60, mac, ip, p));
    assert(!BuildTcp(frame, mac, peer, ip, gateway, 1, 2, 0, 0, TcpAck, 1, frame, 513));
    auto neighbor = [&]()
    {
        ResetNeighbors();
        uint8_t hardware[6];
        ResolveNeighbor(gateway, hardware);
        PollNeighbors(mac);
        BuildArp(frame, 2, peer, gateway, mac, ip);
        ArpPacket arp;
        assert(ParseArp(frame, 60, mac, arp));
        ReceiveNeighbor(frame, arp, mac);
    };
    neighbor();
    HackosTcp r = {};
    __builtin_memcpy(r.address, gateway, 4);
    r.peerPort = 7778;
    r.size = 5;
    __builtin_memcpy(r.data, "hello", 5);
    tick += 100;
    assert(TcpControl(1, HACKOS_TCP_START, r) == 0);
    assert(TcpControl(1, HACKOS_TCP_START, r) == HACKOS_RETRY);
    assert(TcpControl(2, HACKOS_TCP_QUERY, r) == HACKOS_NOT_FOUND);
    auto poll = [&]()
    {
        tick += 2;
        PollTcp(true, true, mac, ip);
        assert(ParseTcp(transmitted, transmittedLength, peer, gateway, p));
    };
    poll();
    assert(p.flags == TcpSyn);
    uint32_t initial = p.sequence;
    uint16_t port = p.sourcePort;
    unsigned count = sends;
    tick += 100;
    poll();
    assert(sends == count + 1 && p.sequence == initial && p.flags == TcpSyn);
    auto reply = [&](uint32_t seq, uint32_t ack, uint8_t flags, const char* data, unsigned size, uint16_t window = 2048)
    {
        unsigned n = BuildTcp(frame, peer, mac, gateway, ip, 7778, port, seq, ack, flags, window, (const uint8_t*)data, size);
        ReceiveTcp(frame, n, mac, ip);
    };
    // Wrong SYN acknowledgement cannot establish the connection.
    reply(0xfffffffd, initial + 2, TcpSyn | TcpAck, nullptr, 0);
    assert(TcpControl(1, HACKOS_TCP_QUERY, r) == 0 && r.state == HACKOS_TCP_PENDING);
    reply(0xfffffffd, initial + 1, TcpSyn | TcpAck, nullptr, 0);
    poll();
    assert(p.flags == TcpAck && p.acknowledgement == 0xfffffffe);
    poll();
    assert(p.size == 5 && !__builtin_memcmp(p.data, "hello", 5));
    // Unacceptable RST and future ACK do not terminate or acknowledge the request.
    reply(17, initial + 6, TcpRst | TcpAck, nullptr, 0);
    reply(0xfffffffe, initial + 99, TcpAck, nullptr, 0);
    poll();
    assert(p.flags == TcpAck);
    // Partial ACK retransmits only the remaining suffix after the RTO.
    reply(0xfffffffe, initial + 3, TcpAck, nullptr, 0);
    tick += 100;
    poll();
    assert(p.sequence == initial + 3 && p.size == 3 && !__builtin_memcmp(p.data, "llo", 3));
    reply(0xfffffffe, initial + 6, TcpAck, nullptr, 0);
    poll();
    assert(p.flags == (TcpFin | TcpAck) && p.sequence == initial + 6);
    // Out-of-order reply must not be buffered or advance the receive sequence.
    reply(1, initial + 7, TcpAck | TcpPush, "lo", 2);
    poll();
    assert(p.acknowledgement == 0xfffffffe);
    reply(0xfffffffe, initial + 7, TcpAck | TcpPush, "hel", 3);
    poll();
    assert(p.acknowledgement == 1);
    // Duplicate prefix is trimmed, and wraparound arithmetic preserves byte order.
    reply(0xfffffffe, initial + 7, TcpAck | TcpFin, "hello", 5);
    poll();
    assert(p.acknowledgement == 4);
    assert(TcpControl(1, HACKOS_TCP_QUERY, r) == 0 && r.state == HACKOS_TCP_COMPLETE);
    assert(r.received == 5 && !__builtin_memcmp(r.data, "hello", 5) && r.retransmissions == 2);
    CancelTcp(1);
    assert(TcpControl(1, HACKOS_TCP_QUERY, r) == HACKOS_NOT_FOUND);
    reply(3, initial + 7, TcpAck | TcpFin, nullptr, 0);
    poll();
    assert(p.acknowledgement == 4); // TIME-WAIT still answers FIN after process cleanup.
    r = {};
    __builtin_memcpy(r.address, gateway, 4);
    r.peerPort = 7778;
    tick += 100;
    assert(TcpControl(2, HACKOS_TCP_START, r) == 0);
    poll();
    port = p.sourcePort;
    initial = p.sequence;
    reply(0, initial + 1, TcpRst | TcpAck, nullptr, 0);
    assert(TcpControl(2, HACKOS_TCP_QUERY, r) == 0 && r.state == HACKOS_TCP_RESET);
    CancelTcp(2);
    r.localPort = 0;
    tick += 100;
    assert(TcpControl(3, HACKOS_TCP_START, r) == 0);
    PollTcp(false, false, mac, ip);
    assert(TcpControl(3, HACKOS_TCP_QUERY, r) == 0 && r.state == HACKOS_TCP_UNAVAILABLE);
    CancelTcp(3);
    r.localPort = 0;
    tick += 100;
    assert(TcpControl(4, HACKOS_TCP_START, r) == 0);
    tick += 2000;
    assert(TcpControl(4, HACKOS_TCP_QUERY, r) == 0 && r.state == HACKOS_TCP_TIMEOUT);
    CancelTcp(4);
    r.localPort = 0;
    r.size = 513;
    assert(TcpControl(4, HACKOS_TCP_START, r) == HACKOS_ERROR);
    r.size = 0;
    r.peerPort = 0;
    assert(TcpControl(4, HACKOS_TCP_START, r) == HACKOS_ERROR);
    // A closed peer receive window prevents data transmission until a valid update.
    r.peerPort = 7778;
    r.size = 1;
    r.data[0] = 'x';
    r.localPort = 0;
    tick += 100;
    neighbor();
    assert(TcpControl(5, HACKOS_TCP_START, r) == 0);
    poll();
    port = p.sourcePort;
    initial = p.sequence;
    reply(100, initial + 1, TcpSyn | TcpAck, nullptr, 0, 0);
    poll();
    count = sends;
    poll();
    assert(sends == count);
    reply(101, initial + 1, TcpAck, nullptr, 0, 1);
    poll();
    assert(p.size == 1 && p.data[0] == 'x');
    // Buffer exhaustion is explicit, never memory corruption or silent truncation.
    char full[512] = {};
    for (unsigned i = 0; i < 4; ++i)
    {
        reply(101 + 512 * i, initial + 2, TcpAck, full, 512);
    }
    reply(2149, initial + 2, TcpAck, "!", 1);
    assert(TcpControl(5, HACKOS_TCP_QUERY, r) == 0 && r.state == HACKOS_TCP_TOO_LARGE);
    poll();
    assert(p.flags & TcpRst);
    CancelTcp(5);
    r = {};
    __builtin_memcpy(r.address, gateway, 4);
    r.peerPort = 7778;
    for (unsigned pid = 10; pid < 18; ++pid)
    {
        tick += 10;
        assert(TcpControl(pid, HACKOS_TCP_START, r) == 0);
    }
    tick += 10;
    assert(TcpControl(18, HACKOS_TCP_START, r) == HACKOS_RETRY);
    // Lease replacement fails all live requests; process cleanup releases their slots.
    InvalidateTcp();
    for (unsigned pid = 10; pid < 18; ++pid)
    {
        assert(TcpControl(pid, HACKOS_TCP_QUERY, r) == 0 && r.state == HACKOS_TCP_UNAVAILABLE);
        CancelTcp(pid);
    }
    tick += 13000;
    PollTcp(false, false, mac, ip);
}
