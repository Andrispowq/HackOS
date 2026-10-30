#include "net/arp.h"
#include <cassert>

static void Copy(uint8_t* destination, const uint8_t* source, unsigned length)
{
    for (unsigned i = 0; i < length; ++i)
    {
        destination[i] = source[i];
    }
}

void TestIcmp();
void TestRoute();
void TestDns();
void TestUdpSockets();
void TestDhcp();

int main()
{
    TestIcmp();
    TestRoute();
    TestDns();
    TestUdpSockets();
    TestDhcp();
    const uint8_t mac[6] = {0x52, 0x54, 0, 0x12, 0x34, 0x56};
    const uint8_t peer[6] = {0x52, 0x55, 10, 0, 2, 2};
    const uint8_t ip[4] = {10, 0, 2, 15}, gateway[4] = {10, 0, 2, 2};
    uint8_t frame[Net::ArpFrameSize];
    Net::ArpPacket result;
    Net::BuildArp(frame, 1, mac, ip, nullptr, gateway);
    assert(Net::ParseArp(frame, sizeof(frame), peer, result));
    assert(result.operation == 1 && Net::EqualBytes(result.senderMac, mac, 6));
    assert(Net::EqualBytes(result.targetIp, gateway, 4));
    for (unsigned i = 42; i < sizeof(frame); ++i)
    {
        assert(frame[i] == 0);
    }
    Net::BuildArp(frame, 2, peer, gateway, mac, ip);
    assert(Net::ParseArp(frame, sizeof(frame), mac, result));
    assert(result.operation == 2 && Net::EqualBytes(result.targetMac, mac, 6));
    for (unsigned length = 0; length < 42; ++length)
    {
        // Supply an allocation of exactly the advertised length to catch overreads.
        auto* shortFrame = new uint8_t[length];
        Copy(shortFrame, frame, length);
        assert(!Net::ParseArp(shortFrame, length, mac, result));
        delete[] shortFrame;
    }
    assert(!Net::ParseArp(nullptr, 60, mac, result));
    assert(!Net::ParseArp(frame, 1515, mac, result));
    const unsigned invalid[] = {0, 6, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22};
    for (unsigned offset : invalid)
    {
        uint8_t damaged[60];
        Copy(damaged, frame, 60);
        damaged[offset] ^= 0x80;
        assert(!Net::ParseArp(damaged, 60, mac, result));
    }
    frame[6] |= 1;
    frame[22] |= 1;
    assert(!Net::ParseArp(frame, 60, mac, result));
}
