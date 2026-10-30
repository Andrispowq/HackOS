#pragma once

#include "lib/stdint.h"

namespace Net
{
constexpr uint32_t ArpFrameSize = 60;

struct ArpPacket
{
    uint16_t operation;
    uint8_t senderMac[6];
    uint8_t senderIp[4];
    uint8_t targetMac[6];
    uint8_t targetIp[4];
};

bool ParseArp(const uint8_t* frame, uint32_t length, const uint8_t* localMac, ArpPacket& packet);
void BuildArp(uint8_t* frame, uint16_t operation, const uint8_t* localMac, const uint8_t* localIp,
              const uint8_t* targetMac, const uint8_t* targetIp);
bool EqualBytes(const uint8_t* a, const uint8_t* b, uint32_t length);
}
