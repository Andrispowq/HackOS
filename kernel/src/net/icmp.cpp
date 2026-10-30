#include "icmp.h"
#include "arp.h"

namespace Net
{
static uint16_t Read16(const uint8_t* data)
{
    return ((uint16_t)data[0] << 8) | data[1];
}

static void Write16(uint8_t* data, uint16_t value)
{
    data[0] = value >> 8;
    data[1] = (uint8_t)value;
}

uint16_t InternetChecksum(const uint8_t* data, unsigned length)
{
    uint32_t sum = 0;
    while (length >= 2)
    {
        sum += Read16(data);
        data += 2;
        length -= 2;
    }
    if (length) sum += (uint16_t)data[0] << 8;
    while (sum >> 16)
    {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

void BuildEcho(uint8_t* frame, const uint8_t* mac, const uint8_t* peerMac, const uint8_t* ip,
               const uint8_t* peerIp, uint16_t identifier, uint16_t sequence, const uint8_t* data)
{
    for (unsigned i = 0; i < EchoFrameSize; ++i)
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
    Write16(header + 2, 20 + 8 + EchoDataSize);
    Write16(header + 4, sequence);
    header[6] = 0x40; // Don't fragment.
    header[8] = 64;
    header[9] = 1;
    for (unsigned i = 0; i < 4; ++i)
    {
        header[12 + i] = ip[i];
        header[16 + i] = peerIp[i];
    }
    Write16(header + 10, InternetChecksum(header, 20));
    uint8_t* echo = header + 20;
    echo[0] = 8;
    Write16(echo + 4, identifier);
    Write16(echo + 6, sequence);
    for (unsigned i = 0; i < EchoDataSize; ++i)
    {
        echo[8 + i] = data[i];
    }
    Write16(echo + 2, InternetChecksum(echo, 8 + EchoDataSize));
}

bool ParseEchoReply(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip,
                    EchoPacket& packet)
{
    if (!frame || length < EchoFrameSize || length > 1514) return false;
    if (!EqualBytes(frame, mac, 6) || frame[12] != 8 || frame[13] != 0) return false;
    const uint8_t* header = frame + 14;
    // No IP options or reassembly in this bounded Echo client. Permit DF only.
    if (header[0] != 0x45 || Read16(header + 2) != EchoFrameSize - 14 ||
        (Read16(header + 6) & 0xbfff) || !header[8] || header[9] != 1 ||
        InternetChecksum(header, 20) || !EqualBytes(header + 16, ip, 4))
    {
        return false;
    }
    const uint8_t* echo = header + 20;
    if (echo[0] != 0 || echo[1] != 0 || InternetChecksum(echo, 8 + EchoDataSize)) return false;
    packet = {Read16(echo + 4), Read16(echo + 6), header[8], header + 12, echo + 8};
    return true;
}
}
