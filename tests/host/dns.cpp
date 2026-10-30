#include "net/dns.h"
#include "net/udp.h"
#include "net/route.h"
#include "net/icmp.h"
#include "hackos/abi.h"
#include "hackos/runtime/hostname.h"
#include <cassert>
#include <initializer_list>

extern volatile uint64_t tick;
extern unsigned sends, transmittedLength;
extern uint8_t transmitted[1514];
static bool randomAvailable = true;
bool RandomSalt(uint8_t salt[16])
{
    static unsigned sequence;
    ++sequence;
    for (unsigned i = 0; i < 16; ++i)
    {
        salt[i] = sequence >> ((i % 4) * 8);
    }
    return randomAvailable;
}

static unsigned Answer(uint8_t* data, uint16_t id, const char* name)
{
    unsigned size = Net::BuildDns(data, name, id);
    data[2] = 0x81;
    data[3] = 0x80;
    data[7] = 1;
    const uint8_t answer[] = {0xc0, 12, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 198, 51, 100, 9};
    __builtin_memcpy(data + size, answer, sizeof(answer));
    return size + sizeof(answer);
}

void TestDns()
{
    char name[256];
    assert(Hackos::NormalizeHostname("EXAMPLE.Com.", name) && !__builtin_strcmp(name, "example.com"));
    for (const char* bad : {"", ".", "a..b", "-a.com", "a-.com", "bad_name.com", "1.2.3.999", " a.com", "a.com "})
    {
        assert(!Hackos::NormalizeHostname(bad, name));
    }
    char huge[256];
    __builtin_memset(huge, 'a', sizeof(huge));
    assert(!Hackos::NormalizeHostname(huge, name));
    huge[64] = 0;
    assert(!Hackos::NormalizeHostname(huge, name));
    uint8_t data[512], frame[Net::UdpFrameMax];
    unsigned size = Answer(data, 123, "demo.test");
    HackosResolve result = {};
    assert(Net::ParseDns(data, size, 123, "demo.test", result));
    assert(result.state == HACKOS_RESOLVE_REPLY && result.ttl == 60 && result.address[0] == 198);
    for (unsigned length = 0; length < size; ++length)
    {
        auto* shortData = new uint8_t[length];
        __builtin_memcpy(shortData, data, length);
        assert(!Net::ParseDns(shortData, length, 123, "demo.test", result));
        delete[] shortData;
    }
    assert(!Net::ParseDns(data, size, 124, "demo.test", result));
    assert(!Net::ParseDns(data, size, 123, "other.test", result));
    for (unsigned offset : {0u, 2u, 3u, 4u, 12u, size - 15, size - 5})
    {
        uint8_t malformed[512];
        __builtin_memcpy(malformed, data, size);
        malformed[offset] ^= 0x40;
        assert(!Net::ParseDns(malformed, size, 123, "demo.test", result));
    }
    unsigned question = Net::BuildDns(data, "demo.test", 123);
    data[2] = 0x81;
    data[3] = 0x83;
    assert(Net::ParseDns(data, question, 123, "demo.test", result) && result.state == HACKOS_RESOLVE_NOT_FOUND);
    data[3] = 0x82;
    assert(Net::ParseDns(data, question, 123, "demo.test", result) && result.state == HACKOS_RESOLVE_SERVER_ERROR);
    data[3] = 0x80;
    data[2] = 0x83;
    assert(Net::ParseDns(data, question, 123, "demo.test", result) && result.state == HACKOS_RESOLVE_TRUNCATED);
    data[2] = 0x81;
    assert(Net::ParseDns(data, question, 123, "demo.test", result) && result.state == HACKOS_RESOLVE_NO_ADDRESS);
    // CNAME and its A record, both compressed, with minimum chain TTL.
    const uint8_t alias[] = {0xc0,12, 0,5,0,1, 0,0,0,20, 0,8, 5,'a','l','i','a','s',0xc0,17};
    __builtin_memcpy(data + question, alias, sizeof(alias));
    unsigned aliasAt = question + 12;
    const uint8_t address[] = {0xc0,(uint8_t)aliasAt, 0,1,0,1, 0,0,0,60, 0,4, 10,0,2,99};
    __builtin_memcpy(data + question + sizeof(alias), address, sizeof(address));
    data[7] = 2;
    size = question + sizeof(alias) + sizeof(address);
    assert(Net::ParseDns(data, size, 123, "demo.test", result));
    assert(result.state == HACKOS_RESOLVE_REPLY && result.ttl == 20 && result.address[3] == 99);
    data[question + 1] = question; // Self-reference is rejected.
    assert(!Net::ParseDns(data, size, 123, "demo.test", result));

    const uint8_t mac[6] = {0x52,0x54,0,0x12,0x34,0x56}, peer[6] = {0x52,0x55,10,0,2,3};
    const uint8_t server[4] = {10,0,2,3};
    size = Answer(data, 123, "demo.test");
    unsigned length = Net::BuildUdp(frame, peer, mac, server, Net::LocalAddress, 53, 50000, data, size);
    Net::UdpPacket packet;
    assert(Net::ParseUdp(frame, length, mac, Net::LocalAddress, packet) && packet.size == size);
    for (unsigned n = 0; n < length; ++n)
    {
        auto* shortFrame = new uint8_t[n];
        __builtin_memcpy(shortFrame, frame, n);
        assert(!Net::ParseUdp(shortFrame, n, mac, Net::LocalAddress, packet));
        delete[] shortFrame;
    }
    frame[length - 1] ^= 1;
    assert(!Net::ParseUdp(frame, length, mac, Net::LocalAddress, packet));
    frame[length - 1] ^= 1;
    frame[40] = frame[41] = 0;
    assert(Net::ParseUdp(frame, length, mac, Net::LocalAddress, packet));
    frame[39] ^= 1;
    assert(!Net::ParseUdp(frame, length, mac, Net::LocalAddress, packet));
    assert(Net::BuildUdp(frame, mac, peer, Net::LocalAddress, server, 50000, 53, data, 513) == 0);

    tick += 10000;
    Net::ResetNeighbors();
    uint8_t hardware[6], arp[60];
    Net::ResolveNeighbor(server, hardware);
    Net::PollNeighbors(mac);
    Net::BuildArp(arp, 2, peer, server, mac, Net::LocalAddress);
    Net::ArpPacket neighbor;
    assert(Net::ParseArp(arp, sizeof(arp), mac, neighbor));
    Net::ReceiveNeighbor(arp, neighbor, mac);
    result = {};
    __builtin_strcpy(result.name, "demo.test");
    randomAvailable = false;
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_START, result) == HACKOS_ERROR);
    randomAvailable = true;
    for (unsigned pid = 100; pid < 108; ++pid)
    {
        assert(Net::ResolveControl(pid, HACKOS_RESOLVE_START, result) == 0);
    }
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_START, result) == HACKOS_RETRY);
    assert(Net::ResolveControl(108, HACKOS_RESOLVE_START, result) == HACKOS_RETRY);
    assert(Net::ResolveControl(108, HACKOS_RESOLVE_QUERY, result) == HACKOS_NOT_FOUND);
    for (unsigned pid = 101; pid < 108; ++pid)
    {
        Net::CancelResolve(pid);
    }
    unsigned before = sends;
    Net::PollResolve(true, true, mac, Net::LocalAddress);
    assert(sends == before + 1);
    assert(Net::ParseUdp(transmitted, transmittedLength, peer, server, packet));
    auto port = packet.sourcePort;
    auto id = (uint16_t)((packet.data[0] << 8) | packet.data[1]);
    size = Answer(data, id + 1, "demo.test");
    length = Net::BuildUdp(frame, peer, mac, server, Net::LocalAddress, 53, port, data, size);
    Net::ReceiveResolve(frame, length, mac, Net::LocalAddress);
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_QUERY, result) == 0 && result.state == HACKOS_RESOLVE_PENDING);
    size = Answer(data, id, "demo.test");
    const uint8_t wrongServer[4] = {10, 0, 2, 99};
    length = Net::BuildUdp(frame, peer, mac, wrongServer, Net::LocalAddress, 53, port, data, size);
    Net::ReceiveResolve(frame, length, mac, Net::LocalAddress);
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_QUERY, result) == 0 && result.state == HACKOS_RESOLVE_PENDING);
    length = Net::BuildUdp(frame, peer, mac, server, Net::LocalAddress, 53, port ^ 1, data, size);
    Net::ReceiveResolve(frame, length, mac, Net::LocalAddress);
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_QUERY, result) == 0 && result.state == HACKOS_RESOLVE_PENDING);
    length = Net::BuildUdp(frame, peer, mac, server, Net::LocalAddress, 54, port, data, size);
    Net::ReceiveResolve(frame, length, mac, Net::LocalAddress);
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_QUERY, result) == 0 && result.state == HACKOS_RESOLVE_PENDING);
    length = Net::BuildUdp(frame, peer, mac, server, Net::LocalAddress, 53, port, data, size);
    Net::ReceiveResolve(frame, length, mac, Net::LocalAddress);
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_QUERY, result) == 0 && result.state == HACKOS_RESOLVE_REPLY);
    assert(result.address[0] == 198);
    Net::CancelResolve(100);
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_QUERY, result) == HACKOS_NOT_FOUND);
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_START, result) == 0);
    Net::PollResolve(true, true, mac, Net::LocalAddress);
    assert(sends == before + 1); // Cancellation cannot bypass rate limiting.
    Net::ReceiveResolve(frame, length, mac, Net::LocalAddress); // Old response cannot complete a new query.
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_QUERY, result) == 0 && result.state == HACKOS_RESOLVE_PENDING);
    for (unsigned i = 0; i < 4; ++i)
    {
        tick += 100;
        Net::PollResolve(true, true, mac, Net::LocalAddress);
    }
    assert(sends == before + 4); // Three bounded transmissions for the second query.
    tick += 100;
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_QUERY, result) == 0 && result.state == HACKOS_RESOLVE_TIMEOUT);
    tick += 1000;
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_QUERY, result) == HACKOS_NOT_FOUND);
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_START, result) == 0);
    Net::PollResolve(false, false, mac, Net::LocalAddress);
    assert(Net::ResolveControl(100, HACKOS_RESOLVE_QUERY, result) == 0 && result.state == HACKOS_RESOLVE_UNAVAILABLE);
    Net::CancelResolve(100);
    assert(Net::ResolveControl(100, 999, result) == HACKOS_ERROR);
    unsigned random = 12345;
    for (unsigned trial = 0; trial < 4000; ++trial)
    {
        size = Answer(data, 123, "demo.test");
        for (unsigned mutation = 0; mutation < 4; ++mutation)
        {
            random = random * 1664525 + 1013904223;
            unsigned offset = (random >> 16) % size;
            data[offset] ^= (uint8_t)random;
        }
        Net::ParseDns(data, size, 123, "demo.test", result);
    }
}
