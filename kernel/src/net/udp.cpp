#include "udp.h"
#include "arp.h"
#include "icmp.h"

namespace Net
{
static uint16_t Read16(const uint8_t* p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static void Write16(uint8_t* p, uint16_t value)
{
    p[0] = value >> 8;
    p[1] = value;
}

static uint16_t Checksum(const uint8_t* ip, const uint8_t* udp, unsigned length)
{
    uint32_t sum = 17 + length;
    for (unsigned i = 12; i < 20; i += 2)
    {
        sum += Read16(ip + i);
    }
    sum += (uint16_t)~InternetChecksum(udp, length);
    while (sum >> 16)
    {
        sum = (sum & 65535) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

unsigned BuildUdp(uint8_t* frame, const uint8_t* mac, const uint8_t* peerMac,
                  const uint8_t* ip, const uint8_t* peerIp, uint16_t sourcePort,
                  uint16_t destinationPort, const uint8_t* data, unsigned size)
{
    if (size > UdpDataMax || !sourcePort || !destinationPort) return 0;
    unsigned length = 42 + size;
    if (length < 60) length = 60;
    for (unsigned i = 0; i < length; ++i)
    {
        frame[i] = 0;
    }
    for (unsigned i = 0; i < 6; ++i)
    {
        frame[i] = peerMac[i];
        frame[6 + i] = mac[i];
    }
    frame[12] = 8;
    uint8_t* header = frame + 14;
    header[0] = 0x45;
    Write16(header + 2, 28 + size);
    header[6] = 0x40;
    header[8] = 64;
    header[9] = 17;
    for (unsigned i = 0; i < 4; ++i)
    {
        header[12 + i] = ip[i];
        header[16 + i] = peerIp[i];
    }
    Write16(header + 10, InternetChecksum(header, 20));
    uint8_t* udp = header + 20;
    Write16(udp, sourcePort);
    Write16(udp + 2, destinationPort);
    Write16(udp + 4, 8 + size);
    for (unsigned i = 0; i < size; ++i)
    {
        udp[8 + i] = data[i];
    }
    uint16_t checksum = Checksum(header, udp, size + 8);
    Write16(udp + 6, checksum ? checksum : 65535);
    return length;
}

bool ParseUdp(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip, UdpPacket& packet, bool bootstrap)
{
    if (!frame || length < 42 || length > 1514) return false;
    static const uint8_t broadcastMac[6] = {255,255,255,255,255,255};
    static const uint8_t broadcastIp[4] = {255,255,255,255};
    if ((!EqualBytes(frame, mac, 6) && !(bootstrap && EqualBytes(frame, broadcastMac, 6))) ||
        frame[12] != 8 || frame[13])
    {
        return false;
    }
    const uint8_t* header = frame + 14;
    unsigned total = Read16(header + 2);
    if (header[0] != 0x45 || total < 28 || total > 28 + (bootstrap ? 548 : UdpDataMax) || total > length - 14 ||
        (Read16(header + 6) & 0xbfff) || !header[8] || header[9] != 17 ||
        InternetChecksum(header, 20) || (!EqualBytes(header + 16, ip, 4) && !(bootstrap && EqualBytes(header + 16, broadcastIp, 4))))
    {
        return false;
    }
    const uint8_t* udp = header + 20;
    if (Read16(udp + 4) != total - 20 || !Read16(udp) || !Read16(udp + 2)) return false;
    // IPv4 permits an absent UDP checksum; nonzero checksums must validate.
    if (Read16(udp + 6) && Checksum(header, udp, total - 20)) return false;
    packet = {header + 12, udp + 8, total - 28, Read16(udp), Read16(udp + 2)};
    return true;
}
}
