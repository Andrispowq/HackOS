#pragma once
#include "hackos/hardware.h"

namespace Net
{
struct DhcpLease
{
    uint8_t address[4], mask[4], gateway[4], dns[4], server[4];
    uint32_t lease, renew, rebind, fields;
    uint8_t type;
};
bool ParseDhcp(const uint8_t* data, unsigned size, uint32_t xid, const uint8_t* mac, DhcpLease& lease);
bool ValidateLease(DhcpLease& lease);
unsigned BuildDhcp(uint8_t* data, uint32_t xid, const uint8_t* mac, unsigned state, const DhcpLease& lease);
void DhcpLink(bool up, const uint8_t* mac);
void PollDhcp(const uint8_t* mac);
void ReceiveDhcp(const uint8_t* frame, unsigned size, const uint8_t* mac);
void SnapshotDhcp(HackosHardwareInfo& info);
}
