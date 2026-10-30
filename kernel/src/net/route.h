#pragma once
#include "arp.h"

namespace Net
{
extern uint8_t LocalAddress[4], SubnetMask[4], GatewayAddress[4], DnsAddress[4];
extern uint64_t ConfigurationGeneration;
bool NetworkConfigured();
void PrintNetworkAddress(const uint8_t* address);
void ConfigureNetwork(const uint8_t* address, const uint8_t* mask, const uint8_t* gateway, const uint8_t* dns);
enum class NeighborState : uint8_t { Missing, Pending, Resolved, Failed };
struct NeighborInfo
{
    NeighborState state;
    uint8_t mac[6];
    uint32_t attempts;
};
// Connected subnet and default route; excludes unsupported destination classes.
bool NextHop(const uint8_t* destination, uint8_t* hop);
NeighborInfo InspectNeighbor(const uint8_t* address);
NeighborState ResolveNeighbor(const uint8_t* address, uint8_t* mac);
void ReceiveNeighbor(const uint8_t* frame, const ArpPacket& packet, const uint8_t* localMac);
void PollNeighbors(const uint8_t* localMac);
void ResetNeighbors();
unsigned NeighborCount();
}
