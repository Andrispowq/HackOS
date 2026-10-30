#pragma once
#include "hackos/network.h"

namespace Net
{
constexpr uint8_t TcpFin = 1, TcpSyn = 2, TcpRst = 4, TcpPush = 8, TcpAck = 16;
struct TcpPacket
{
    const uint8_t* source;
    const uint8_t* data;
    unsigned size;
    uint32_t sequence, acknowledgement;
    uint16_t sourcePort, destinationPort, window, mss;
    uint8_t flags;
};
unsigned BuildTcp(uint8_t* frame, const uint8_t* mac, const uint8_t* peerMac,
                  const uint8_t* ip, const uint8_t* peerIp, uint16_t sourcePort,
                  uint16_t destinationPort, uint32_t sequence, uint32_t acknowledgement,
                  uint8_t flags, uint16_t window, const uint8_t* data, unsigned size);
bool ParseTcp(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip, TcpPacket& packet);
uint64_t TcpControl(uint64_t pid, uint64_t operation, HackosTcp& record);
void CancelTcp(uint64_t pid);
void InvalidateTcp();
void PollTcp(bool ready, bool link, const uint8_t* mac, const uint8_t* ip);
void ReceiveTcp(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip);
}
