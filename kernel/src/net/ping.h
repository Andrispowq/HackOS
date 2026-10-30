#pragma once
#include "hackos/network.h"

namespace Net
{
uint64_t EchoControl(uint64_t pid, uint64_t operation, HackosEcho& record);
void CancelEcho(uint64_t pid);
// Driver calls these under its preemption guard; no allocation or blocking.
void PollEcho(bool ready, bool link, bool resolved, const uint8_t* mac, const uint8_t* peerMac,
              const uint8_t* ip, const uint8_t* peerIp);
void ReceiveEcho(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip);
}
