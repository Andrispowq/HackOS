#pragma once
#include "hackos/network.h"

namespace Net
{
uint64_t UdpControl(uint64_t pid, uint64_t operation, uint64_t handle, HackosDatagram& record);
// RECEIVE peeks; dispatcher commits only after a successful user copy, under its guard.
void ConsumeUdp(uint64_t pid, uint64_t handle);
void CloseUdpSockets(uint64_t pid);
void PollUdpSockets(bool ready, bool link, const uint8_t* mac, const uint8_t* ip);
void ReceiveUdpSockets(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip);
}
