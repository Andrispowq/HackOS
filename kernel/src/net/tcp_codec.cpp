#include "tcp.h"
#include "arp.h"
#include "icmp.h"

namespace Net
{
namespace
{
uint16_t Read16(const uint8_t* p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}
uint32_t Read32(const uint8_t* p)
{
    return ((uint32_t)Read16(p) << 16) | Read16(p + 2);
}
void Write16(uint8_t* p, uint16_t value)
{
    p[0] = value >> 8;
    p[1] = value;
}
void Write32(uint8_t* p, uint32_t value)
{
    Write16(p, value >> 16);
    Write16(p + 2, value);
}
uint16_t Checksum(const uint8_t* ip, const uint8_t* tcp, unsigned size)
{
    uint32_t sum = 6 + size + (uint16_t)~InternetChecksum(tcp, size);
    for (unsigned i = 12; i < 20; i += 2)
    {
        sum += Read16(ip + i);
    }
    while (sum >> 16)
    {
        sum = (sum & 65535) + (sum >> 16);
    }
    return (uint16_t)~sum;
}
}

unsigned BuildTcp(uint8_t* frame, const uint8_t* mac, const uint8_t* peerMac,
                  const uint8_t* ip, const uint8_t* peerIp, uint16_t sourcePort,
                  uint16_t destinationPort, uint32_t sequence, uint32_t acknowledgement,
                  uint8_t flags, uint16_t window, const uint8_t* data, unsigned size)
{
    if (size > 512 || !sourcePort || !destinationPort || (size && !data)) return 0;
    unsigned headerSize = flags & TcpSyn ? 24 : 20;
    unsigned total = 20 + headerSize + size, length = 14 + total;
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
    auto* ipHeader = frame + 14;
    ipHeader[0] = 0x45;
    Write16(ipHeader + 2, total);
    ipHeader[6] = 0x40;
    ipHeader[8] = 64;
    ipHeader[9] = 6;
    for (unsigned i = 0; i < 4; ++i)
    {
        ipHeader[12 + i] = ip[i];
        ipHeader[16 + i] = peerIp[i];
    }
    Write16(ipHeader + 10, InternetChecksum(ipHeader, 20));
    auto* tcp = ipHeader + 20;
    Write16(tcp, sourcePort);
    Write16(tcp + 2, destinationPort);
    Write32(tcp + 4, sequence);
    Write32(tcp + 8, acknowledgement);
    tcp[12] = (headerSize / 4) << 4;
    tcp[13] = flags;
    Write16(tcp + 14, window);
    if (flags & TcpSyn)
    {
        tcp[20] = 2;
        tcp[21] = 4;
        Write16(tcp + 22, 512);
    }
    for (unsigned i = 0; i < size; ++i)
    {
        tcp[headerSize + i] = data[i];
    }
    Write16(tcp + 16, Checksum(ipHeader, tcp, headerSize + size));
    return length;
}

bool ParseTcp(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip, TcpPacket& packet)
{
    if (!frame || length < 54 || length > 1514 || !EqualBytes(frame, mac, 6) || frame[12] != 8 || frame[13]) return false;
    const uint8_t* header = frame + 14;
    unsigned total = Read16(header + 2);
    if (header[0] != 0x45 || total < 40 || total > 1500 || total > length - 14 ||
        (Read16(header + 6) & 0xbfff) || !header[8] || header[9] != 6 ||
        InternetChecksum(header, 20) || !EqualBytes(header + 16, ip, 4))
    {
        return false;
    }
    const uint8_t* tcp = header + 20;
    unsigned headerSize = (tcp[12] >> 4) * 4;
    if (headerSize < 20 || headerSize > total - 20 || !Read16(tcp) || !Read16(tcp + 2) ||
        Checksum(header, tcp, total - 20))
    {
        return false;
    }
    uint16_t mss = 536;
    bool seenMss = false;
    for (unsigned at = 20; at < headerSize;)
    {
        uint8_t option = tcp[at++];
        if (!option) break;
        if (option == 1) continue;
        if (at >= headerSize || tcp[at] < 2 || tcp[at] > headerSize - at + 1) return false;
        unsigned size = tcp[at++];
        if (option == 2)
        {
            if (size != 4 || seenMss || !(tcp[13] & TcpSyn)) return false;
            mss = Read16(tcp + at);
            if (!mss) return false;
            seenMss = true;
        }
        at += size - 2;
    }
    packet = {header + 12, tcp + headerSize, total - 20 - headerSize,
              Read32(tcp + 4), Read32(tcp + 8), Read16(tcp), Read16(tcp + 2), Read16(tcp + 14), mss, tcp[13]};
    return true;
}
}
