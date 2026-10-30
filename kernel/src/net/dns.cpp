#include "dns.h"
#include "hackos/runtime/hostname.h"

namespace Net
{
namespace
{
uint16_t Read16(const uint8_t* p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

bool Same(const char* a, const char* b)
{
    for (unsigned i = 0; i < 256; ++i)
    {
        if (a[i] != b[i]) return false;
        if (!a[i]) return true;
    }
    return false;
}

bool Name(const uint8_t* data, unsigned size, unsigned& cursor, char (&name)[256])
{
    unsigned at = cursor, used = 0, jumps = 0;
    bool jumped = false;
    for (unsigned work = 0; work < 256; ++work)
    {
        if (at >= size) return false;
        unsigned label = data[at++];
        if ((label & 0xc0) == 0xc0)
        {
            if (at >= size || ++jumps > 16) return false;
            unsigned target = ((label & 63) << 8) | data[at++];
            if (target >= at - 2) return false; // Backward references only; no pointer cycles.
            if (!jumped) cursor = at;
            jumped = true;
            at = target;
            continue;
        }
        if (label > 63 || at + label > size) return false;
        if (!label)
        {
            if (!jumped) cursor = at;
            name[used] = 0;
            return true;
        }
        if (used)
        {
            if (used >= 253) return false;
            name[used++] = '.';
        }
        if (used + label > 253) return false;
        for (unsigned i = 0; i < label; ++i)
        {
            char c = data[at++];
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
            name[used++] = c;
        }
    }
    return false;
}
}

unsigned BuildDns(uint8_t* data, const char* name, uint16_t id)
{
    char normalized[256];
    if (!Hackos::NormalizeHostname(name, normalized)) return 0;
    for (unsigned i = 0; i < 12; ++i)
    {
        data[i] = 0;
    }
    data[0] = id >> 8;
    data[1] = id;
    data[2] = 1; // Recursion desired.
    data[5] = 1;
    unsigned at = 12, begin = 0;
    do
    {
        unsigned end = begin;
        while (normalized[end] && normalized[end] != '.')
        {
            ++end;
        }
        data[at++] = end - begin;
        for (unsigned i = begin; i < end; ++i)
        {
            data[at++] = normalized[i];
        }
        if (!normalized[end]) break;
        begin = end + 1;
    } while (true);
    data[at++] = 0;
    data[at++] = 0;
    data[at++] = 1; // A
    data[at++] = 0;
    data[at++] = 1; // IN
    return at;
}

bool ParseDns(const uint8_t* data, unsigned size, uint16_t id, const char* name, HackosResolve& result)
{
    if (!data || size < 12 || size > 512 || Read16(data) != id ||
        !(data[2] & 0x80) || (data[2] & 0x78) || (data[3] & 0x40) || Read16(data + 4) != 1)
    {
        return false;
    }
    unsigned at = 12;
    char question[256];
    if (!Name(data, size, at, question) || !Same(question, name) || at + 4 > size ||
        Read16(data + at) != 1 || Read16(data + at + 2) != 1)
    {
        return false;
    }
    at += 4;
    result.errorCode = data[3] & 15;
    if (data[2] & 2)
    {
        result.state = HACKOS_RESOLVE_TRUNCATED;
        return true;
    }
    if (result.errorCode)
    {
        result.state = result.errorCode == 3 ? HACKOS_RESOLVE_NOT_FOUND : HACKOS_RESOLVE_SERVER_ERROR;
        return true;
    }
    unsigned answers = Read16(data + 6);
    unsigned total = answers + Read16(data + 8) + Read16(data + 10);
    if (total > 32) return false;
    struct Record { unsigned owner, value, length; uint16_t type, klass; uint32_t ttl; } records[32];
    // Validate the entire message before accepting any answer.
    for (unsigned i = 0; i < total; ++i)
    {
        unsigned owner = at;
        if (!Name(data, size, at, question) || at + 10 > size) return false;
        uint32_t ttl = ((uint32_t)Read16(data + at + 4) << 16) | Read16(data + at + 6);
        records[i] = {owner, at + 10, Read16(data + at + 8), Read16(data + at), Read16(data + at + 2), ttl};
        at += 10;
        if (records[i].length > size - at) return false;
        if (records[i].type == 1 && records[i].klass == 1 && records[i].length != 4) return false;
        if (records[i].type == 5 && records[i].klass == 1)
        {
            unsigned end = at;
            if (!Name(data, size, end, question) || end != at + records[i].length) return false;
        }
        at += records[i].length;
    }
    if (at != size) return false;
    char current[256];
    for (unsigned i = 0; i < 256; ++i)
    {
        current[i] = name[i];
        if (!name[i]) break;
    }
    uint32_t ttl = 0xffffffff;
    for (unsigned depth = 0; depth <= 8; ++depth)
    {
        bool alias = false;
        for (unsigned i = 0; i < answers; ++i)
        {
            const auto& record = records[i];
            unsigned owner = record.owner;
            if (!Name(data, size, owner, question)) return false;
            if (record.klass != 1 || !Same(question, current)) continue;
            if (record.type == 1)
            {
                for (unsigned j = 0; j < 4; ++j)
                {
                    result.address[j] = data[record.value + j];
                }
                result.ttl = record.ttl < ttl ? record.ttl : ttl;
                result.state = HACKOS_RESOLVE_REPLY;
                return true;
            }
            if (record.type == 5)
            {
                unsigned value = record.value;
                if (!Name(data, size, value, current)) return false;
                if (record.ttl < ttl) ttl = record.ttl;
                alias = true;
                break;
            }
        }
        if (!alias) break;
    }
    result.state = HACKOS_RESOLVE_NO_ADDRESS;
    return true;
}
}
