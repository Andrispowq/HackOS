#include "net/icmp.h"
#include "net/arp.h"
#include "net/ping.h"
#include "net/route.h"
#include "hackos/abi.h"
#include <cassert>
#include <initializer_list>

volatile uint64_t tick;
unsigned sends;
unsigned arpSends;
uint8_t transmitted[1514];
unsigned transmittedLength;
void SerialText(const char*)
{
}

void WakeStreamWaiters()
{
}

void DisablePreemption()
{
}
void EnablePreemption()
{
}
uint32_t TimerFrequency()
{
    return 100;
}
uint64_t TimerTicksForMilliseconds(uint32_t ms)
{
    return (ms + 9) / 10;
}
namespace Net
{
bool Transmit(const uint8_t* frame, uint32_t length)
{
    if (length == ArpFrameSize && frame[13] == 6)
    {
        ++arpSends;
        return true;
    }
    assert(length <= sizeof(transmitted));
    transmittedLength = length;
    for (unsigned i = 0; i < length; ++i)
    {
        transmitted[i] = frame[i];
    }
    ++sends;
    return true;
}
}

static void Checksum(uint8_t* data, unsigned length, unsigned offset)
{
    data[offset] = data[offset + 1] = 0;
    uint16_t sum = Net::InternetChecksum(data, length);
    data[offset] = sum >> 8;
    data[offset + 1] = (uint8_t)sum;
}

void TestIcmp()
{
    const uint8_t mac[6] = {0x52, 0x54, 0, 0x12, 0x34, 0x56}, peer[6] = {0x52, 0x55, 10, 0, 2, 2};
    const uint8_t ip[4] = {10, 0, 2, 15}, gateway[4] = {10, 0, 2, 2};
    const uint8_t checksumVector[] = {0, 1, 0xf2, 3, 0xf4, 0xf5, 0xf6, 0xf7};
    assert(Net::InternetChecksum(checksumVector, 8) == 0x220d);
    const uint8_t odd[] = {1, 2, 3};
    assert(Net::InternetChecksum(odd, 3) == 0xfbfd);
    uint8_t data[Net::EchoDataSize] = {}, frame[Net::EchoFrameSize];
    Net::BuildEcho(frame, peer, mac, gateway, ip, 123, 456, data);
    frame[34] = 0;
    Checksum(frame + 34, 40, 2);
    Net::EchoPacket packet;
    assert(Net::ParseEchoReply(frame, sizeof(frame), mac, ip, packet));
    assert(packet.identifier == 123 && packet.sequence == 456 && packet.ttl == 64);
    for (unsigned size = 0; size < sizeof(frame); ++size)
    {
        auto* shortened = new uint8_t[size];
        for (unsigned i = 0; i < size; ++i)
        {
            shortened[i] = frame[i];
        }
        assert(!Net::ParseEchoReply(shortened, size, mac, ip, packet));
        delete[] shortened;
    }
    assert(!Net::ParseEchoReply(nullptr, sizeof(frame), mac, ip, packet));
    assert(!Net::ParseEchoReply(frame, 1515, mac, ip, packet));
    for (unsigned offset : {0U, 12U, 13U, 14U, 16U, 17U, 20U, 21U, 22U, 23U, 24U, 30U, 34U, 35U, 36U, 42U})
    {
        frame[offset] ^= 0x80;
        assert(!Net::ParseEchoReply(frame, sizeof(frame), mac, ip, packet));
        frame[offset] ^= 0x80;
    }
    // Structurally unsupported headers remain rejected with a correct checksum.
    for (unsigned offset : {14U, 20U, 21U, 23U, 30U})
    {
        frame[offset] ^= 1;
        Checksum(frame + 14, 20, 10);
        assert(!Net::ParseEchoReply(frame, sizeof(frame), mac, ip, packet));
        frame[offset] ^= 1;
        Checksum(frame + 14, 20, 10);
    }
    HackosEcho record = {{10, 0, 2, 2}, 0, 0, 0, 0};
    for (uint64_t pid = 1; pid <= 8; ++pid)
    {
        assert(Net::EchoControl(pid, HACKOS_ECHO_START, record) == 0);
    }
    assert(Net::EchoControl(1, HACKOS_ECHO_START, record) == HACKOS_RETRY);
    assert(Net::EchoControl(9, HACKOS_ECHO_START, record) == HACKOS_RETRY);
    assert(Net::EchoControl(9, HACKOS_ECHO_QUERY, record) == HACKOS_NOT_FOUND);
    for (uint64_t pid = 2; pid <= 8; ++pid)
    {
        Net::CancelEcho(pid);
    }
    uint8_t resolvedMac[6], arp[Net::ArpFrameSize];
    assert(Net::ResolveNeighbor(gateway, resolvedMac) == Net::NeighborState::Pending);
    Net::PollNeighbors(mac);
    Net::BuildArp(arp, 2, peer, gateway, mac, ip);
    Net::ArpPacket neighbor;
    assert(Net::ParseArp(arp, sizeof(arp), mac, neighbor));
    Net::ReceiveNeighbor(arp, neighbor, mac);
    Net::PollEcho(true, true, mac, ip);
    assert(sends == 1);
    auto reply = [&]()
    {
        uint16_t id = (transmitted[38] << 8) | transmitted[39];
        uint16_t sequence = (transmitted[40] << 8) | transmitted[41];
        Net::BuildEcho(frame, peer, mac, gateway, ip, id, sequence, transmitted + 42);
        frame[34] = 0;
        Checksum(frame + 34, 40, 2);
    };
    reply();
    // A valid-checksum reply with a different nonce must not complete the request.
    frame[42] ^= 1;
    Checksum(frame + 34, 40, 2);
    Net::ReceiveEcho(frame, sizeof(frame), mac, ip);
    assert(Net::EchoControl(1, HACKOS_ECHO_QUERY, record) == 0 && record.state == HACKOS_ECHO_PENDING);
    reply();
    frame[41] ^= 1;
    Checksum(frame + 34, 40, 2);
    Net::ReceiveEcho(frame, sizeof(frame), mac, ip);
    assert(Net::EchoControl(1, HACKOS_ECHO_QUERY, record) == 0 && record.state == HACKOS_ECHO_PENDING);
    reply();
    tick = 2;
    Net::ReceiveEcho(frame, sizeof(frame), mac, ip);
    assert(Net::EchoControl(1, HACKOS_ECHO_QUERY, record) == 0 && record.state == HACKOS_ECHO_REPLY);
    assert(record.elapsedMs == 20 && record.ttl == 64);
    // A new request cannot accept an old response; cancellation cannot bypass rate limiting.
    assert(Net::EchoControl(1, HACKOS_ECHO_START, record) == 0);
    Net::PollEcho(true, true, mac, ip);
    assert(sends == 1);
    Net::ReceiveEcho(frame, sizeof(frame), mac, ip);
    assert(Net::EchoControl(1, HACKOS_ECHO_QUERY, record) == 0 && record.state == HACKOS_ECHO_PENDING);
    tick = 302;
    assert(Net::EchoControl(1, HACKOS_ECHO_QUERY, record) == 0 && record.state == HACKOS_ECHO_TIMEOUT);
    tick = 1302;
    assert(Net::EchoControl(1, HACKOS_ECHO_QUERY, record) == HACKOS_NOT_FOUND);
    assert(Net::EchoControl(1, HACKOS_ECHO_START, record) == 0);
    Net::PollEcho(false, false, mac, ip);
    assert(Net::EchoControl(1, HACKOS_ECHO_QUERY, record) == 0 && record.state == HACKOS_ECHO_UNAVAILABLE);
    Net::CancelEcho(1);
    assert(Net::EchoControl(1, HACKOS_ECHO_QUERY, record) == HACKOS_NOT_FOUND);
    record.address[3] = 0;
    assert(Net::EchoControl(1, HACKOS_ECHO_START, record) == HACKOS_ERROR);
    assert(Net::EchoControl(1, 999, record) == HACKOS_ERROR);
}
