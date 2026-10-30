#pragma once
#include "hackos/types.h"

namespace Net
{
constexpr unsigned UdpDataMax = 512;
constexpr unsigned UdpFrameMax = 14 + 20 + 8 + UdpDataMax;
struct UdpPacket
{
    const uint8_t* source;
    const uint8_t* data;
    unsigned size;
    uint16_t sourcePort, destinationPort;
};
unsigned BuildUdp(uint8_t* frame, const uint8_t* mac, const uint8_t* peerMac,
                  const uint8_t* ip, const uint8_t* peerIp, uint16_t sourcePort,
                  uint16_t destinationPort, const uint8_t* data, unsigned size);
bool ParseUdp(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip, UdpPacket& packet, bool bootstrap = false);
}
