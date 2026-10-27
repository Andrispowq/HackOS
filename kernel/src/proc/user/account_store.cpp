#include "account_store.h"
#include "security/crypto.h"
#include "lib/memory.h"

AccountStore accountStore;
static constexpr uint64_t Magic = 0x3154434341534f48;

bool AccountStore::Valid(AccountImage& image)
{
    if (image.magic != Magic || image.version != 1 || image.length != sizeof(AccountData) || !image.generation ||
        image.reserved || !AccountRegistry::Validate(image.data))
    {
        return false;
    }

    uint8_t expected[32], digest[32];
    memcpy(expected, image.digest, 32);
    memset(image.digest, 0, 32);
    Sha256(&image, sizeof(image), digest);
    memcpy(image.digest, expected, 32);
    return ConstantEqual(expected, digest, 32);
}

AccountStore::State AccountStore::Load(AccountIO* device, AccountRegistry& registry)
{
    io = device;
    state = Broken;
    generation = 0;
    active = 1;
    recovered = false;
    if (!io || !io->Read(0, &images[0]) || !io->Read(1, &images[1]))
    {
        return state;
    }

    bool valid[2] = {Valid(images[0]), Valid(images[1])};
    bool empty[2] = {true, true};
    for (unsigned slot = 0; slot < 2; ++slot)
    {
        const auto bytes = (const uint8_t*)&images[slot];
        for (unsigned i = 0; i < sizeof(AccountImage); ++i)
        {
            empty[slot] = empty[slot] && bytes[i] == 0;
        }
    }

    if (!valid[0] && !valid[1])
    {
        if (empty[0] && empty[1])
        {
            registry.Initialise();
            state = Empty;
        }

        return state;
    }

    if (valid[0] && valid[1] && images[0].generation == images[1].generation &&
        !ConstantEqual(&images[0], &images[1], sizeof(AccountImage)))
    {
        return state;
    }

    active = valid[1] && (!valid[0] || images[1].generation > images[0].generation) ? 1 : 0;
    generation = images[active].generation;
    registry.Restore(images[active].data);
    recovered = !valid[1 - active] && !empty[1 - active];
    state = Ready;
    return state;
}

bool AccountStore::Commit(const AccountRegistry& registry)
{
    if (!io || state == Broken || generation == ~0ULL || !AccountRegistry::Validate(registry.Data()))
    {
        return false;
    }

    unsigned target = 1 - active;
    auto& image = images[target];
    memset(&image, 0, sizeof(image));
    image.magic = Magic;
    image.version = 1;
    image.length = sizeof(AccountData);
    image.generation = generation + 1;
    image.data = registry.Data();
    Sha256(&image, sizeof(image), image.digest);
    if (!io->Write(target, &image) || !io->Flush() || !io->Read(target, &images[active]) ||
        !ConstantEqual(&image, &images[active], sizeof(image)))
    {
        // A failed flush has an uncertain on-disk outcome. Freeze further
        // authentication/updates until reboot revalidates both generations.
        state = Broken;
        return false;
    }

    active = target;
    ++generation;
    state = Ready;
    recovered = false;
    return true;
}
