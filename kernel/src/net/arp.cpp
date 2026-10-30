#include "arp.h"

namespace Net
{
static const uint8_t broadcast[6] = {255, 255, 255, 255, 255, 255};

bool EqualBytes(const uint8_t* a, const uint8_t* b, uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i)
    {
        if (a[i] != b[i]) return false;
    }
    return true;
}

static void Copy(uint8_t* destination, const uint8_t* source, uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i)
    {
        destination[i] = source[i];
    }
}

bool ParseArp(const uint8_t* frame, uint32_t length, const uint8_t* localMac, ArpPacket& packet)
{
    if (!frame || length < 42 || length > 1514) return false;
    if (!EqualBytes(frame, localMac, 6) && !EqualBytes(frame, broadcast, 6)) return false;
    // Ethernet II, Ethernet/IPv4 ARP, six-byte MACs and four-byte IPs.
    if (frame[12] != 8 || frame[13] != 6 || frame[14] != 0 || frame[15] != 1 ||
        frame[16] != 8 || frame[17] != 0 || frame[18] != 6 || frame[19] != 4 ||
        frame[20] != 0 || (frame[21] != 1 && frame[21] != 2))
    {
        return false;
    }
    const uint8_t zero[6] = {};
    if ((frame[22] & 1) || EqualBytes(frame + 22, zero, 6) || !EqualBytes(frame + 6, frame + 22, 6)) return false;
    packet.operation = frame[21];
    Copy(packet.senderMac, frame + 22, 6);
    Copy(packet.senderIp, frame + 28, 4);
    Copy(packet.targetMac, frame + 32, 6);
    Copy(packet.targetIp, frame + 38, 4);
    return true;
}

void BuildArp(uint8_t* frame, uint16_t operation, const uint8_t* localMac, const uint8_t* localIp,
              const uint8_t* targetMac, const uint8_t* targetIp)
{
    for (uint32_t i = 0; i < ArpFrameSize; ++i)
    {
        frame[i] = 0;
    }
    Copy(frame, operation == 1 ? broadcast : targetMac, 6);
    Copy(frame + 6, localMac, 6);
    frame[12] = 8;
    frame[13] = 6;
    frame[15] = 1;
    frame[16] = 8;
    frame[18] = 6;
    frame[19] = 4;
    frame[21] = (uint8_t)operation;
    Copy(frame + 22, localMac, 6);
    Copy(frame + 28, localIp, 4);
    if (operation == 2)
    {
        Copy(frame + 32, targetMac, 6);
    }
    Copy(frame + 38, targetIp, 4);
}
}
