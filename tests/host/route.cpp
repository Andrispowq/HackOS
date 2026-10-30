#include "net/route.h"
#include "net/ping.h"
#include "net/icmp.h"
#include "hackos/abi.h"
#include "hackos/runtime/ipv4.h"
#include <cassert>
#include <initializer_list>

extern volatile uint64_t tick;
extern unsigned sends, arpSends;
extern uint8_t transmitted[1514];

static const uint8_t mac[6] = {0x52, 0x54, 0, 0x12, 0x34, 0x56};
static const uint8_t peer[6] = {0x52, 0x55, 10, 0, 2, 99};
static const uint8_t gatewayMac[6] = {0x52, 0x55, 10, 0, 2, 2};
static void Learn(const uint8_t* ip, const uint8_t* hardware)
{
    uint8_t frame[Net::ArpFrameSize];
    Net::BuildArp(frame, 2, hardware, ip, mac, Net::LocalAddress);
    Net::ArpPacket packet;
    assert(Net::ParseArp(frame, sizeof(frame), mac, packet));
    Net::ReceiveNeighbor(frame, packet, mac);
}

static void Checksum(uint8_t* data, unsigned length, unsigned at)
{
    data[at] = data[at + 1] = 0;
    auto value = Net::InternetChecksum(data, length);
    data[at] = value >> 8;
    data[at + 1] = (uint8_t)value;
}

void TestRoute()
{
    using Net::NeighborState;
    uint8_t address[4], hop[4], hardware[6];
    for (const char* text : {"", "1.2.3", "1.2.3.4.5", "256.2.3.4", "01.2.3.4", " 1.2.3.4", "1.2.3.4 ", "1..3.4", "1.2.3.-1"})
    {
        assert(!Hackos::ParseIPv4(text, address));
    }
    assert(Hackos::ParseIPv4("10.0.2.99", address));
    assert(Net::NextHop(address, hop) && Net::EqualBytes(address, hop, 4));
    assert(Hackos::ParseIPv4("203.0.113.9", address));
    assert(Net::NextHop(address, hop) && Net::EqualBytes(hop, Net::GatewayAddress, 4));
    for (const char* text : {"0.1.2.3", "127.0.0.1", "169.254.1.1", "224.0.0.1", "255.255.255.255", "10.0.2.0", "10.0.2.255", "10.0.2.15"})
    {
        assert(Hackos::ParseIPv4(text, address));
        assert(!Net::NextHop(address, hop));
    }
    tick = 20000;
    Net::ResetNeighbors();
    const uint8_t neighborIp[4] = {10, 0, 2, 99};
    Learn(neighborIp, peer);
    assert(Net::InspectNeighbor(neighborIp).state == NeighborState::Missing);
    assert(Net::ResolveNeighbor(neighborIp, hardware) == NeighborState::Pending);
    Learn(neighborIp, peer); // A reply before an actual probe must not populate the cache.
    assert(Net::InspectNeighbor(neighborIp).state == NeighborState::Pending);
    Net::PollNeighbors(mac);
    Learn(neighborIp, peer);
    assert(Net::ResolveNeighbor(neighborIp, hardware) == NeighborState::Resolved);
    assert(Net::EqualBytes(hardware, peer, 6));
    Learn(neighborIp, gatewayMac); // Unsolicited replacement is ignored.
    assert(Net::ResolveNeighbor(neighborIp, hardware) == NeighborState::Resolved);
    assert(Net::EqualBytes(hardware, peer, 6));
    tick += 6000;
    assert(Net::InspectNeighbor(neighborIp).state == NeighborState::Missing);
    assert(Net::ResolveNeighbor(neighborIp, hardware) == NeighborState::Pending);
    unsigned before = arpSends;
    for (unsigned i = 0; i < 5; ++i)
    {
        Net::PollNeighbors(mac);
        tick += 50;
    }
    Net::PollNeighbors(mac);
    assert(arpSends == before + 5);
    assert(Net::ResolveNeighbor(neighborIp, hardware) == NeighborState::Failed);
    tick += 500;
    assert(Net::ResolveNeighbor(neighborIp, hardware) == NeighborState::Pending);
    Net::ResetNeighbors();
    for (unsigned i = 30; i < 46; ++i)
    {
        uint8_t next[4] = {10, 0, 2, (uint8_t)i};
        assert(Net::ResolveNeighbor(next, hardware) == NeighborState::Pending);
    }
    assert(Net::ResolveNeighbor(neighborIp, hardware) == NeighborState::Pending);
    assert(Net::InspectNeighbor(neighborIp).state == NeighborState::Missing); // No pending eviction.
    Net::ResetNeighbors();
    tick += 100;
    assert(Net::ResolveNeighbor(neighborIp, hardware) == NeighborState::Pending);
    Net::PollNeighbors(mac);
    Learn(neighborIp, peer);
    HackosEcho request = {{10, 0, 2, 99}, 0, 0, 0, {0}};
    assert(Net::EchoControl(123, HACKOS_ECHO_START, request) == 0);
    Net::PollEcho(true, true, mac, Net::LocalAddress);
    assert(Net::EqualBytes(transmitted, peer, 6));
    assert(Net::EqualBytes(transmitted + 30, neighborIp, 4));
    Net::CancelEcho(123);
    tick += 100;
    assert(Net::ResolveNeighbor(Net::GatewayAddress, hardware) == NeighborState::Pending);
    Net::PollNeighbors(mac);
    Learn(Net::GatewayAddress, gatewayMac);
    request.address[0] = 203;
    request.address[1] = 0;
    request.address[2] = 113;
    request.address[3] = 9;
    assert(Net::EchoControl(123, HACKOS_ECHO_START, request) == 0);
    Net::PollEcho(true, true, mac, Net::LocalAddress);
    assert(Net::EqualBytes(transmitted, gatewayMac, 6));
    assert(Net::EqualBytes(transmitted + 30, request.address, 4));
    // Minimal RFC 792 error quote (IP header + 8 bytes) and fully quoted errors.
    uint8_t error[14 + 20 + 8 + 60] = {};
    for (unsigned i = 0; i < 6; ++i)
    {
        error[i] = mac[i];
        error[6 + i] = gatewayMac[i];
    }
    error[12] = 8;
    error[14] = 0x45;
    error[17] = 88;
    error[22] = 64;
    error[23] = 1;
    for (unsigned i = 0; i < 4; ++i)
    {
        error[26 + i] = Net::GatewayAddress[i];
        error[30 + i] = Net::LocalAddress[i];
    }
    error[34] = 3;
    error[35] = 1;
    for (unsigned i = 0; i < 60; ++i)
    {
        error[42 + i] = transmitted[14 + i];
    }
    Checksum(error + 14, 20, 10);
    Checksum(error + 34, 68, 2);
    Net::EchoError parsed;
    assert(Net::ParseEchoError(error, sizeof(error), mac, Net::LocalAddress, parsed));
    for (unsigned length = 0; length < 70; ++length)
    {
        auto* shortFrame = new uint8_t[length];
        for (unsigned i = 0; i < length; ++i)
        {
            shortFrame[i] = error[i];
        }
        assert(!Net::ParseEchoError(shortFrame, length, mac, Net::LocalAddress, parsed));
        delete[] shortFrame;
    }
    // Reject malformed headers even when all affected checksums are repaired.
    for (unsigned offset : {14u, 17u, 20u, 22u, 23u, 26u, 30u, 34u, 35u,
                            42u, 45u, 48u, 51u, 54u, 62u, 63u})
    {
        uint8_t malformed[sizeof(error)];
        for (unsigned i = 0; i < sizeof(error); ++i)
        {
            malformed[i] = error[i];
        }
        malformed[offset] = offset == 17 ? 255 : offset == 35 ? 16 : 0;
        if (offset == 20 || offset == 48) malformed[offset] = 0x20; // More fragments.
        Checksum(malformed + 42, 20, 10);
        Checksum(malformed + 34, 68, 2);
        Checksum(malformed + 14, 20, 10);
        // The original code is zero; use an invalid Echo request code instead.
        if (offset == 63)
        {
            malformed[offset] = 1;
            Checksum(malformed + 34, 68, 2);
        }
        assert(!Net::ParseEchoError(malformed, sizeof(malformed), mac, Net::LocalAddress, parsed));
    }
    error[74] ^= 1; // Wrong quoted payload with otherwise valid checksums.
    Checksum(error + 34, 68, 2);
    Net::ReceiveEcho(error, sizeof(error), mac, Net::LocalAddress);
    assert(Net::EchoControl(123, HACKOS_ECHO_QUERY, request) == 0 && request.state == HACKOS_ECHO_PENDING);
    error[74] ^= 1;
    error[17] = 56;
    Checksum(error + 14, 20, 10);
    Checksum(error + 34, 36, 2);
    assert(Net::ParseEchoError(error, 70, mac, Net::LocalAddress, parsed));
    assert(parsed.dataSize == 0);
    Net::ReceiveEcho(error, 70, mac, Net::LocalAddress);
    assert(Net::EchoControl(123, HACKOS_ECHO_QUERY, request) == 0 && request.state == HACKOS_ECHO_UNREACHABLE);
    assert(request.errorCode == 1);
    Net::CancelEcho(123);
    Net::ResetNeighbors();
    assert(Net::NeighborCount() == 0);
}
