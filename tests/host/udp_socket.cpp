#include "net/socket.h"
#include "net/udp.h"
#include "net/route.h"
#include "hackos/abi.h"
#include <cassert>

extern volatile uint64_t tick;
extern unsigned sends, transmittedLength;
extern uint8_t transmitted[1514];

void TestUdpSockets()
{
    const uint8_t mac[6] = {0x52,0x54,0,0x12,0x34,0x56}, peer[6] = {0x52,0x55,10,0,2,2};
    HackosDatagram record = {};
    record.address[0] = 10;
    record.address[2] = 2;
    record.address[3] = 2;
    record.peerPort = 7777;
    record.localPort = 53;
    assert(Net::UdpControl(1, HACKOS_UDP_OPEN, 0, record) == HACKOS_ERROR);
    record.localPort = 49152;
    assert(Net::UdpControl(1, HACKOS_UDP_OPEN, 0, record) == HACKOS_ERROR);
    record.localPort = 8000;
    uint64_t handle = Net::UdpControl(1, HACKOS_UDP_OPEN, 0, record);
    assert(handle && !(handle >> 63));
    assert(Net::UdpControl(2, HACKOS_UDP_OPEN, 0, record) == HACKOS_RETRY);
    assert(Net::UdpControl(2, HACKOS_UDP_STATUS, handle, record) == HACKOS_NOT_FOUND);
    assert(Net::UdpControl(2, HACKOS_UDP_CLOSE, handle, record) == HACKOS_NOT_FOUND);
    assert(Net::UdpControl(1, 99, handle, record) == HACKOS_ERROR);
    assert(Net::UdpControl(1, HACKOS_UDP_RECEIVE, handle, record) == HACKOS_RETRY);
    record.localPort = 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        assert(!(Net::UdpControl(1, HACKOS_UDP_OPEN, 0, record) >> 63));
    }
    assert(Net::UdpControl(1, HACKOS_UDP_OPEN, 0, record) == HACKOS_RETRY);
    for (unsigned pid = 2; pid <= 4; ++pid)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            assert(!(Net::UdpControl(pid, HACKOS_UDP_OPEN, 0, record) >> 63));
        }
    }
    assert(Net::UdpControl(5, HACKOS_UDP_OPEN, 0, record) == HACKOS_RETRY);
    for (unsigned pid = 2; pid <= 4; ++pid)
    {
        Net::CloseUdpSockets(pid);
    }
    record.size = 513;
    assert(Net::UdpControl(1, HACKOS_UDP_SEND, handle, record) == HACKOS_ERROR);
    record.size = 512;
    for (unsigned i = 0; i < 512; ++i)
    {
        record.data[i] = (uint8_t)i;
    }
    assert(Net::UdpControl(1, HACKOS_UDP_SEND, handle, record) == 0);
    assert(Net::UdpControl(1, HACKOS_UDP_SEND, handle, record) == HACKOS_RETRY);
    tick += 10000;
    assert(Net::UdpControl(1, HACKOS_UDP_STATUS, handle, record) == 0 && record.state == HACKOS_UDP_TIMEOUT);
    Net::ResetNeighbors();
    uint8_t hardware[6], arp[60];
    Net::ResolveNeighbor(Net::GatewayAddress, hardware);
    Net::PollNeighbors(mac);
    Net::BuildArp(arp, 2, peer, Net::GatewayAddress, mac, Net::LocalAddress);
    Net::ArpPacket neighbor;
    assert(Net::ParseArp(arp, sizeof(arp), mac, neighbor));
    Net::ReceiveNeighbor(arp, neighbor, mac);
    record.size = 512;
    for (unsigned i = 0; i < 512; ++i)
    {
        record.data[i] = (uint8_t)i;
    }
    assert(Net::UdpControl(1, HACKOS_UDP_SEND, handle, record) == 0);
    unsigned before = sends;
    Net::PollUdpSockets(true, true, mac, Net::LocalAddress);
    assert(sends == before + 1);
    Net::UdpPacket packet;
    assert(Net::ParseUdp(transmitted, transmittedLength, peer, Net::GatewayAddress, packet));
    assert(packet.sourcePort == 8000 && packet.destinationPort == 7777 && packet.size == 512);
    for (unsigned i = 0; i < 512; ++i)
    {
        assert(packet.data[i] == (uint8_t)i);
    }
    assert(Net::UdpControl(1, HACKOS_UDP_STATUS, handle, record) == 0 && record.state == HACKOS_UDP_SENT && record.sent == 1);
    uint8_t frame[Net::UdpFrameMax], data[512];
    for (unsigned i = 0; i < 512; ++i)
    {
        data[i] = i;
    }
    unsigned length = Net::BuildUdp(frame, peer, mac, Net::GatewayAddress, Net::LocalAddress, 7778, 8000, data, 512);
    Net::ReceiveUdpSockets(frame, length, mac, Net::LocalAddress);
    const uint8_t wrong[4] = {10,0,2,99};
    length = Net::BuildUdp(frame, peer, mac, wrong, Net::LocalAddress, 7777, 8000, data, 512);
    Net::ReceiveUdpSockets(frame, length, mac, Net::LocalAddress);
    assert(Net::UdpControl(1, HACKOS_UDP_RECEIVE, handle, record) == HACKOS_RETRY);
    // Queue overflow drops the newest datagram, retaining order and boundaries.
    for (unsigned i = 0; i < 7; ++i)
    {
        data[0] = i;
        length = Net::BuildUdp(frame, peer, mac, Net::GatewayAddress, Net::LocalAddress, 7777, 8000, data, i == 3 ? 0 : 512);
        Net::ReceiveUdpSockets(frame, length, mac, Net::LocalAddress);
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        assert(Net::UdpControl(1, HACKOS_UDP_RECEIVE, handle, record) == 0);
        assert(record.queued == 4 - i && record.dropped == 3 && record.size == (i == 3 ? 0 : 512));
        if (i != 3) assert(record.data[0] == i);
        assert(Net::UdpControl(1, HACKOS_UDP_RECEIVE, handle, record) == 0); // Peek is non-consuming.
        Net::ConsumeUdp(2, handle); // Other PIDs cannot drain it.
        Net::ConsumeUdp(1, handle);
    }
    assert(Net::UdpControl(1, HACKOS_UDP_RECEIVE, handle, record) == HACKOS_RETRY);
    // A second send waits for the global limiter; link loss completes it explicitly.
    record.size = 0;
    assert(Net::UdpControl(1, HACKOS_UDP_SEND, handle, record) == 0);
    Net::PollUdpSockets(true, true, mac, Net::LocalAddress);
    assert(sends == before + 1);
    Net::PollUdpSockets(true, false, mac, Net::LocalAddress);
    assert(Net::UdpControl(1, HACKOS_UDP_STATUS, handle, record) == 0 && record.state == HACKOS_UDP_UNAVAILABLE);
    assert(Net::UdpControl(1, HACKOS_UDP_CLOSE, handle, record) == 0);
    assert(Net::UdpControl(1, HACKOS_UDP_STATUS, handle, record) == HACKOS_NOT_FOUND);
    record.localPort = 8000;
    uint64_t next = Net::UdpControl(1, HACKOS_UDP_OPEN, 0, record);
    assert(next != handle && !(next >> 63));
    Net::CloseUdpSockets(1);
    assert(Net::UdpControl(1, HACKOS_UDP_STATUS, next, record) == HACKOS_NOT_FOUND);
    record.localPort = 0;
    record.address[3] = 200;
    next = Net::UdpControl(1, HACKOS_UDP_OPEN, 0, record);
    record.size = 1;
    assert(Net::UdpControl(1, HACKOS_UDP_SEND, next, record) == 0);
    Net::PollUdpSockets(true, true, mac, Net::LocalAddress);
    tick += 250;
    Net::PollUdpSockets(true, true, mac, Net::LocalAddress);
    assert(Net::UdpControl(1, HACKOS_UDP_STATUS, next, record) == 0 && record.state == HACKOS_UDP_NEIGHBOR_FAILED);
    Net::CloseUdpSockets(1);
}
