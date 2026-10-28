#include "boot_fat.h"

static BootReadSector readSector;
static uint8_t sector[512];
static uint32_t partition, total, fatStart, fatSize, dataStart, clusters, clusterSectors;
static uint32_t firstCluster, fileSize, cursorCluster, cursorIndex;

static uint32_t U16(const uint8_t* p)
{
    return p[0] | ((uint32_t)p[1] << 8);
}

static uint32_t U32(const uint8_t* p)
{
    return U16(p) | (U16(p + 2) << 16);
}

static int ValidCluster(uint32_t c)
{
    return c >= 2 && c - 2 < clusters;
}

static int Read(uint32_t relative)
{
    return relative < total && readSector(partition + relative, sector);
}

static int Next(uint32_t c, uint32_t* next)
{
    if (!ValidCluster(c) || !Read(fatStart + c / 128))
    {
        return 0;
    }
    *next = U32(sector + (c % 128) * 4) & 0x0fffffff;
    return *next >= 0x0ffffff8 || ValidCluster(*next);
}

/* Validate the entire chain before trusting it; Floyd detects directory cycles. */
static int Chain(uint32_t first, uint32_t expected)
{
    uint32_t slow = first, fast = first;
    for (uint32_t count = 1; count <= clusters; ++count)
    {
        if (!Next(slow, &slow))
        {
            return 0;
        }
        if (slow >= 0x0ffffff8)
        {
            return !expected || count == expected;
        }
        if (expected && count >= expected)
        {
            return 0;
        }
        for (unsigned i = 0; i < 2 && fast < 0x0ffffff8; ++i)
        {
            if (!Next(fast, &fast))
            {
                return 0;
            }
        }

        if (slow == fast)
        {
            return 0;
        }
    }

    return 0;
}

int BootFatOpen(BootReadSector read)
{
    readSector = read;
    fileSize = 0;
    if (!readSector(0, sector) || U16(sector + 510) != 0xaa55)
    {
        return 0;
    }
    unsigned found = 0;
    uint32_t partitionSize = 0;
    for (unsigned i = 0; i < 4; ++i)
    {
        const uint8_t* p = sector + 446 + i * 16;
        if ((p[0] == 0 || p[0] == 0x80) && (p[4] == 0x0b || p[4] == 0x0c))
        {
            ++found;
            partition = U32(p + 8);
            partitionSize = U32(p + 12);
        }
    }

    if (found != 1 || partition < 78 || !partitionSize || (uint64_t)partition + partitionSize > 0x100000000ULL ||
        !readSector(partition, sector) || U16(sector + 510) != 0xaa55 || U16(sector + 11) != 512 || U16(sector + 17) ||
        U16(sector + 19) || U16(sector + 22) || U16(sector + 42))
    {
        return 0;
    }

    clusterSectors = sector[13];
    fatStart = U16(sector + 14);
    uint32_t fats = sector[16];
    total = U32(sector + 32);
    fatSize = U32(sector + 36);
    uint32_t flags = U16(sector + 40);
    uint32_t root = U32(sector + 44);
    uint64_t data = fatStart + (uint64_t)fats * fatSize;
    if (!clusterSectors || clusterSectors > 128 || (clusterSectors & (clusterSectors - 1)) || !fatStart || !fats ||
        fats > 2 || !fatSize || total > partitionSize || data >= total || (flags & ~0x008fU) ||
        ((flags & 0x80) && (flags & 15) >= fats))
    {
        return 0;
    }

    dataStart = data;
    clusters = (total - dataStart) / clusterSectors;
    if (clusters < 65525 || clusters > 0x0fffffee || (uint64_t)fatSize * 128 < clusters + 2)
    {
        return 0;
    }
    if (flags & 0x80)
    {
        fatStart += (flags & 15) * fatSize;
    }
    if (!Chain(root, 0))
    {
        return 0;
    }

    for (uint32_t c = root; ValidCluster(c);)
    {
        for (uint32_t s = 0; s < clusterSectors; ++s)
        {
            if (!Read(dataStart + (c - 2) * clusterSectors + s))
            {
                return 0;
            }
            for (unsigned off = 0; off < 512; off += 32)
            {
                const uint8_t* p = sector + off;
                if (!p[0])
                {
                    return 0;
                }
                if (p[0] == 0xe5 || (p[11] & 0x18) || p[11] == 0x0f)
                {
                    continue;
                }
                const char* name = "KERNEL  ELF";
                unsigned n = 0;
                while (n < 11 && p[n] == (uint8_t)name[n])
                {
                    ++n;
                }
                if (n != 11)
                {
                    continue;
                }

                firstCluster = (U16(p + 20) << 16) | U16(p + 26);
                fileSize = U32(p + 28);
                uint32_t bytes = clusterSectors * 512;
                if (!fileSize || !Chain(firstCluster, ((uint64_t)fileSize + bytes - 1) / bytes))
                {
                    return 0;
                }
                cursorCluster = firstCluster;
                cursorIndex = 0;
                return 1;
            }
        }

        if (!Next(c, &c))
        {
            return 0;
        }
    }

    return 0;
}

uint32_t BootFatSize(void)
{
    return fileSize;
}

int BootFatRead(uint64_t offset, void* output, uint64_t size)
{
    if (offset > fileSize || size > fileSize - offset)
    {
        return 0;
    }
    uint8_t* dest = output;
    uint32_t bytes = clusterSectors * 512;
    while (size)
    {
        uint32_t index = offset / bytes;
        if (index < cursorIndex)
        {
            cursorCluster = firstCluster;
            cursorIndex = 0;
        }

        while (cursorIndex < index)
        {
            if (!Next(cursorCluster, &cursorCluster) || !ValidCluster(cursorCluster))
            {
                return 0;
            }
            ++cursorIndex;
        }

        if (!Read(dataStart + (cursorCluster - 2) * clusterSectors + (offset % bytes) / 512))
        {
            return 0;
        }
        unsigned start = offset % 512;
        unsigned count = 512 - start;
        if (count > size)
        {
            count = size;
        }
        for (unsigned i = 0; i < count; ++i)
        {
            *dest++ = sector[start + i];
        }

        offset += count;
        size -= count;
    }

    return 1;
}
