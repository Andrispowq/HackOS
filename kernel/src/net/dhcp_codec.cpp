#include "dhcp.h"
#include "arp.h"

namespace Net
{
namespace
{
uint32_t Read32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
void Write32(uint8_t* p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
    {
        p[i] = value >> (24 - 8 * i);
    }
}
bool Unicast(const uint8_t* ip)
{
    return ip[0] && ip[0] != 127 && ip[0] < 224 && !(ip[0] == 169 && ip[1] == 254);
}
}

bool ParseDhcp(const uint8_t* data, unsigned size, uint32_t xid, const uint8_t* mac, DhcpLease& lease)
{
    if (!data || size < 244 || size > 548 || data[0] != 2 || data[1] != 1 || data[2] != 6 ||
        Read32(data + 4) != xid || Read32(data + 24) || !EqualBytes(data + 28, mac, 6) ||
        Read32(data + 236) != 0x63825363)
    {
        return false;
    }
    lease = {};
    for (unsigned i = 0; i < 4; ++i)
    {
        lease.address[i] = data[16 + i];
    }
    unsigned at = 240;
    bool end = false;
    while (at < size)
    {
        uint8_t option = data[at++];
        if (!option) continue;
        if (option == 255)
        {
            end = true;
            break;
        }
        if (at == size) return false;
        unsigned length = data[at++];
        if (length > size - at) return false;
        unsigned bit = 0;
        switch (option)
        {
        case 1:
            bit = 1;
            break;
        case 3:
            bit = 2;
            break;
        case 6:
            bit = 4;
            break;
        case 51:
            bit = 8;
            break;
        case 53:
            bit = 16;
            break;
        case 54:
            bit = 32;
            break;
        case 58:
            bit = 64;
            break;
        case 59:
            bit = 128;
            break;
        case 52: return false; // Option-overloaded BOOTP fields are not supported.
        }
        if (bit)
        {
            if (lease.fields & bit) return false;
            if (option == 53 ? length != 1 : (option == 3 || option == 6) ? (!length || length % 4) : length != 4) return false;
            lease.fields |= bit;
            if (option == 53) lease.type = data[at];
            if (option == 51) lease.lease = Read32(data + at);
            if (option == 58) lease.renew = Read32(data + at);
            if (option == 59) lease.rebind = Read32(data + at);
            uint8_t* address = option == 1 ? lease.mask : option == 3 ? lease.gateway :
                               option == 6 ? lease.dns : option == 54 ? lease.server : nullptr;
            if (address)
            {
                for (unsigned i = 0; i < 4; ++i)
                {
                    address[i] = data[at + i];
                }
            }
        }
        at += length;
    }
    return end && (lease.fields & 48) == 48 && Unicast(lease.server) &&
           (lease.type == 2 || lease.type == 5 || lease.type == 6);
}

bool ValidateLease(DhcpLease& lease)
{
    if ((lease.fields & 63) != 63 || !Unicast(lease.address) || !Unicast(lease.gateway) ||
        !Unicast(lease.dns) || !Unicast(lease.server) || lease.lease < 8)
    {
        return false;
    }
    uint32_t mask = Read32(lease.mask), host = ~mask, ip = Read32(lease.address), gateway = Read32(lease.gateway);
    if (!mask || host < 3 || (host & (host + 1)) || !(ip & host) || (ip & host) == host ||
        (ip & mask) != (gateway & mask) || !(gateway & host) || (gateway & host) == host || ip == gateway)
    {
        return false;
    }
    const uint8_t* peers[] = {lease.dns, lease.server};
    for (const uint8_t* peer : peers)
    {
        uint32_t value = Read32(peer);
        if (value == ip || ((value & mask) == (ip & mask) && (!(value & host) || (value & host) == host))) return false;
    }
    if (!(lease.fields & 64)) lease.renew = lease.lease / 2;
    if (!(lease.fields & 128)) lease.rebind = (uint64_t)lease.lease * 7 / 8;
    return lease.renew && lease.renew < lease.rebind && lease.rebind < lease.lease;
}

unsigned BuildDhcp(uint8_t* data, uint32_t xid, const uint8_t* mac, unsigned state, const DhcpLease& lease)
{
    for (unsigned i = 0; i < 300; ++i)
    {
        data[i] = 0;
    }
    data[0] = data[1] = 1;
    data[2] = 6;
    Write32(data + 4, xid);
    bool leased = state == HACKOS_DHCP_RENEWING || state == HACKOS_DHCP_REBINDING;
    if (!leased) data[10] = 0x80;
    for (unsigned i = 0; i < 6; ++i)
    {
        data[28 + i] = mac[i];
    }
    if (leased)
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            data[12 + i] = lease.address[i];
        }
    }
    Write32(data + 236, 0x63825363);
    unsigned at = 240;
    data[at++] = 53;
    data[at++] = 1;
    data[at++] = state == HACKOS_DHCP_SELECTING ? 1 : 3;
    data[at++] = 61;
    data[at++] = 7;
    data[at++] = 1;
    for (unsigned i = 0; i < 6; ++i)
    {
        data[at++] = mac[i];
    }
    if (state == HACKOS_DHCP_REQUESTING)
    {
        data[at++] = 50;
        data[at++] = 4;
        for (unsigned i = 0; i < 4; ++i)
        {
            data[at++] = lease.address[i];
        }
        data[at++] = 54;
        data[at++] = 4;
        for (unsigned i = 0; i < 4; ++i)
        {
            data[at++] = lease.server[i];
        }
    }
    data[at++] = 55;
    data[at++] = 6;
    const uint8_t options[] = {1, 3, 6, 51, 58, 59};
    for (uint8_t option : options)
    {
        data[at++] = option;
    }
    data[at++] = 57;
    data[at++] = 2;
    data[at++] = 2;
    data[at++] = 64; // Maximum IP message size: 576.
    data[at] = 255;
    return 300;
}
}
