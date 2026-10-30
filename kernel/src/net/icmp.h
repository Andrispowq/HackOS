#pragma once
#include "lib/stdint.h"

namespace Net
{
constexpr unsigned EchoDataSize = 32;
constexpr unsigned EchoFrameSize = 14 + 20 + 8 + EchoDataSize;
struct EchoPacket
{
    uint16_t identifier, sequence;
    uint8_t ttl;
    const uint8_t* source;
    const uint8_t* data;
};
struct EchoError
{
    uint16_t identifier, sequence, checksum;
    uint8_t type, code;
    const uint8_t* destination;
    const uint8_t* data;
    unsigned dataSize;
};
bool ParseEchoError(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip,
                    EchoError& packet);
uint16_t InternetChecksum(const uint8_t* data, unsigned length);
void BuildEcho(uint8_t* frame, const uint8_t* mac, const uint8_t* peerMac, const uint8_t* ip,
               const uint8_t* peerIp, uint16_t identifier, uint16_t sequence, const uint8_t* data);
bool ParseEchoReply(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip,
                    EchoPacket& packet);
}
