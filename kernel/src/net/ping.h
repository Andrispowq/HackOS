#pragma once
#include "hackos/network.h"

namespace Net
{
void InvalidateEcho();
uint64_t EchoControl(uint64_t pid, uint64_t operation, HackosEcho& record);
void CancelEcho(uint64_t pid);
// Driver calls these under its preemption guard; no allocation or blocking.
void PollEcho(bool ready, bool link, const uint8_t* mac, const uint8_t* ip);
void ReceiveEcho(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip);
}
