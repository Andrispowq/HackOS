#include "journal.h"
#include "lib/memory.h"
#include "security/crypto.h"

JournalDevice::JournalDevice(Device* device, uint32_t start, uint32_t sectors)
    : disk(device), start(start), sectors(sectors)
{
    addresses = (uint64_t*)kmalloc(2048 + JOURNAL_LIMIT * 512);
    data = addresses ? (uint8_t*)addresses + 2048 : nullptr;
    permissions = (uint8_t*)kmalloc(PERMISSION_SECTORS * 512);
    if (!addresses || !permissions || !disk || sectors < STORAGE_RESERVED || start > disk->SectorCount() ||
        sectors > disk->SectorCount() - start)
    {
        return;
    }

    JournalHeader header;
    if (!disk->Read(start + 8, &header, 1) || memcmp(header.magic, (void*)"HKFSJ016", 8) || header.version != 1 ||
        header.count > JOURNAL_LIMIT)
    {
        return;
    }

    uint8_t digest[32], expected[32];
    memcpy(expected, header.digest, 32);
    // The header checksum is stored in reserved[0..31]. The digest binds the
    // address table and payload, while the header checksum protects count.
    uint8_t checksum[32];
    memcpy(checksum, header.reserved, 32);
    memset(header.reserved, 0, 32);
    Sha256(&header, sizeof(header), digest);
    if (!ConstantEqual(digest, checksum, 32))
    {
        return;
    }

    count = header.count;
    healthy = true;
    if (count)
    {
        if (!disk->Read(start + 9, addresses, 4 + count))
        {
            healthy = false;
            return;
        }

        Sha256(addresses, 2048 + count * 512, digest);
        if (!ConstantEqual(digest, expected, 32))
        {
            healthy = false;
            return;
        }

        for (unsigned i = 0; i < count; ++i)
        {
            if (!Target(addresses[i]))
            {
                healthy = false;
                return;
            }

            for (unsigned j = 0; j < i; ++j)
            {
                if (addresses[i] == addresses[j])
                {
                    healthy = false;
                    return;
                }
            }
        }

        healthy = Replay() && Header(0);
    }

    count = 0;
    healthy = healthy && disk->Read(start + PERMISSION_START, permissions, PERMISSION_SECTORS);
}

JournalDevice::~JournalDevice()
{
    kfree(addresses);
    kfree(permissions);
}

DeviceType JournalDevice::GetType() const
{
    return disk->GetType();
}

uint64_t JournalDevice::SectorCount() const
{
    return disk->SectorCount();
}

bool JournalDevice::Healthy() const
{
    return healthy;
}

bool JournalDevice::Target(uint64_t lba) const
{
    return lba >= start && lba - start < sectors &&
           (lba - start == 1 || lba - start == 7 ||
            (lba - start >= PERMISSION_START && lba - start < PERMISSION_START + PERMISSION_SECTORS) ||
            lba - start >= STORAGE_RESERVED);
}

bool JournalDevice::Read(uint64_t lba, void* buffer, uint64_t length)
{
    if (!healthy || !buffer || lba < start || lba - start > sectors || length > sectors - (lba - start))
    {
        return false;
    }

    auto out = (uint8_t*)buffer;
    for (uint64_t n = 0; n < length; ++n)
    {
        unsigned i = 0;
        while (i < count && addresses[i] != lba + n)
        {
            ++i;
        }

        if (i < count)
        {
            memcpy(out, data + i * 512, 512);
        }
        else if (lba + n >= start + PERMISSION_START && lba + n < start + PERMISSION_START + PERMISSION_SECTORS)
        {
            memcpy(out, permissions + (lba + n - start - PERMISSION_START) * 512, 512);
        }
        else if (!disk->Read(lba + n, out, 1))
        {
            healthy = false;
            return false;
        }

        out += 512;
    }

    return true;
}

bool JournalDevice::Write(uint64_t lba, void* buffer, uint64_t length)
{
    if (!active || !healthy || !buffer || lba < start || lba - start > sectors || length > sectors - (lba - start))
    {
        return false;
    }

    auto source = (uint8_t*)buffer;
    for (uint64_t n = 0; n < length; ++n)
    {
        if (!Target(lba + n))
        {
            return false;
        }

        uint8_t original[512];
        if (!Read(lba + n, original, 1))
        {
            return false;
        }

        if (!memcmp(original, source + n * 512, 512))
        {
            continue;
        }

        unsigned i = 0;
        while (i < count && addresses[i] != lba + n)
        {
            ++i;
        }

        if (i == JOURNAL_LIMIT)
        {
            return false;
        }

        addresses[i] = lba + n;
        memcpy(data + i * 512, source + n * 512, 512);
        if (i == count)
        {
            ++count;
        }
    }

    return true;
}

bool JournalDevice::Flush()
{
    // Within a transaction this is an ordering boundary in the overlay.
    // Finish is the only durable publication point.
    healthy = healthy && (active || disk->Flush());
    return healthy;
}

bool JournalDevice::Begin()
{
    if (!healthy || active)
    {
        return false;
    }

    count = 0;
    memset(addresses, 0, 2048);
    active = true;
    return true;
}

bool JournalDevice::Header(uint32_t length)
{
    JournalHeader header = {};
    memcpy(header.magic, "HKFSJ016", 8);
    header.version = 1;
    header.count = length;
    if (length)
    {
        Sha256(addresses, 2048 + length * 512, header.digest);
    }

    Sha256(&header, sizeof(header), header.reserved);
    return disk->Write(start + 8, &header, 1) && disk->Flush();
}

bool JournalDevice::Replay()
{
    for (unsigned i = 0; i < count; ++i)
    {
        if (!disk->Write(addresses[i], data + i * 512, 1))
        {
            return false;
        }
    }

    return disk->Flush();
}

bool JournalDevice::Finish(bool success)
{
    if (!active)
    {
        return false;
    }

    active = false;
    if (!success || !healthy)
    {
        count = 0;
        return false;
    }

    if (count)
    {
        healthy =
            disk->Write(start + 9, addresses, 4 + count) && disk->Flush() && Header(count) && Replay() && Header(0);
        if (healthy)
        {
            for (unsigned i = 0; i < count; ++i)
            {
                if (addresses[i] >= start + PERMISSION_START &&
                    addresses[i] < start + PERMISSION_START + PERMISSION_SECTORS)
                {
                    memcpy(permissions + (addresses[i] - start - PERMISSION_START) * 512, data + i * 512, 512);
                }
            }
        }
    }
    else
    {
        healthy = disk->Flush();
    }

    count = 0;
    return healthy;
}
