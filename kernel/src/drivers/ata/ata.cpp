#include "ata.h"
#include "arch/x86_64/ports.h"

static bool WaitATA(bool data)
{
    for (unsigned retry = 0; retry < 1000000; ++retry)
    {
        uint8_t status = __inb(0x1f7);
        if (status == 0 || status == 0xff)
        {
            return false;
        }

        if (!(status & 0x80))
        {
            if (status & 0x21)
            {
                return false;
            }

            if (!data || (status & 8))
            {
                return true;
            }
        }
    }

    return false;
}

static bool BeginATA(uint32_t lba, uint8_t sectors, uint8_t command)
{
    if (!sectors || !WaitATA(false))
    {
        return false;
    }

    __outb(0x1f6, 0xe0 | ((lba >> 24) & 15));
    for (unsigned i = 0; i < 4; ++i)
    {
        __inb(0x3f6);
    }

    __outb(0x1f2, sectors);
    __outb(0x1f3, lba);
    __outb(0x1f4, lba >> 8);
    __outb(0x1f5, lba >> 16);
    __outb(0x1f7, command);
    return true;
}

bool ReadSectorsATA(uint64_t target, uint32_t lba, uint8_t sectors)
{
    if (!BeginATA(lba, sectors, 0x20))
    {
        return false;
    }

    auto words = (uint16_t*)target;
    for (unsigned sector = 0; sector < sectors; ++sector)
    {
        if (!WaitATA(true))
        {
            return false;
        }

        for (unsigned i = 0; i < 256; ++i)
        {
            *words++ = __inw(0x1f0);
        }
    }

    return WaitATA(false);
}

bool WriteSectorsATA(uint32_t lba, uint8_t sectors, uint16_t* words)
{
    if (!BeginATA(lba, sectors, 0x30))
    {
        return false;
    }

    for (unsigned sector = 0; sector < sectors; ++sector)
    {
        if (!WaitATA(true))
        {
            return false;
        }

        for (unsigned i = 0; i < 256; ++i)
        {
            __outw(0x1f0, *words++);
        }
    }

    return WaitATA(false);
}

bool FlushATA()
{
    if (!WaitATA(false))
    {
        return false;
    }

    __outb(0x1f7, 0xe7);
    return WaitATA(false);
}

uint64_t IdentifyATA()
{
    if (!BeginATA(0, 1, 0xec) || !WaitATA(true))
    {
        return 0;
    }

    uint16_t words[256];
    for (unsigned i = 0; i < 256; ++i)
    {
        words[i] = __inw(0x1f0);
    }

    uint64_t sectors = words[60] | ((uint32_t)words[61] << 16);
    return WaitATA(false) && sectors < (1ULL << 28) ? sectors : 0;
}
