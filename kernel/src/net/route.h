#pragma once
#include "arp.h"

namespace Net
{
extern const uint8_t LocalAddress[4], SubnetMask[4], GatewayAddress[4];
enum class NeighborState : uint8_t { Missing, Pending, Resolved, Failed };
struct NeighborInfo
{
    NeighborState state;
    uint8_t mac[6];
    uint32_t attempts;
};
// Static connected /24 and default route; excludes unsupported destination classes.
bool NextHop(const uint8_t* destination, uint8_t* hop);
NeighborInfo InspectNeighbor(const uint8_t* address);
NeighborState ResolveNeighbor(const uint8_t* address, uint8_t* mac);
void ReceiveNeighbor(const uint8_t* frame, const ArpPacket& packet, const uint8_t* localMac);
void PollNeighbors(const uint8_t* localMac);
void ResetNeighbors();
unsigned NeighborCount();
}
