#include "support.hpp"
#include "proc/user/account_store.h"
#include "security/crypto.h"

class MemoryAccounts : public AccountIO
{
  public:
    uint8_t slots[2][8192] = {};
    bool readError = false, flushError = false;
    int tornWrite = -1;
    bool corruptWrite = false;

    bool Read(unsigned slot, void* buffer) override
    {
        if (readError)
        {
            return false;
        }

        test_copy(buffer, slots[slot], 8192);
        return true;
    }

    bool Write(unsigned slot, void* buffer) override
    {
        test_copy(slots[slot], buffer, tornWrite < 0 ? 8192 : tornWrite);
        if (corruptWrite)
        {
            slots[slot][101] ^= 1;
        }

        return tornWrite < 0;
    }

    bool Flush() override
    {
        return !flushError;
    }
};

static void Administrator(AccountRegistry& registry)
{
    registry.Initialise();
    PasswordRecord password = {};
    password.enabled = 1;
    password.iterations = PASSWORD_ITERATIONS;
    password.salt[0] = 7;
    password.digest[0] = 42;
    registry.Setup("admin", password);
}

TEST(account_store_roundtrip_and_generation)
{
    MemoryAccounts disk;
    AccountStore store;
    AccountRegistry registry;
    CHECK(store.Load(&disk, registry) == AccountStore::Empty);
    Administrator(registry);
    CHECK(store.Commit(registry));
    CHECK(registry.CreateGroup("staff") == 1001);
    CHECK(registry.CreateUser("alice", 1001) == 1001);
    CHECK(registry.AddGroup(1001, 1000) == 0);
    CHECK(store.Commit(registry));
    AccountRegistry restored;
    AccountStore reboot;
    CHECK(reboot.Load(&disk, restored) == AccountStore::Ready);
    CHECK(test_name_equal(restored.User(0)->name, "admin"));
    CHECK(restored.Credentials(1001)->groups[0] == 1000);
    CHECK(restored.Data().passwords[0].digest[0] == 42);
    CHECK(restored.CreateUser("next", 1000) == 1002);
    return 0;
}

TEST(account_store_torn_update_recovers_previous)
{
    MemoryAccounts disk;
    AccountStore store;
    AccountRegistry registry;
    CHECK(store.Load(&disk, registry) == AccountStore::Empty);
    Administrator(registry);
    CHECK(store.Commit(registry));
    registry.CreateUser("alice", 1000);
    disk.tornWrite = 100;
    CHECK(!store.Commit(registry));
    CHECK(store.Status() == AccountStore::Broken);
    CHECK(!store.Commit(registry));
    AccountStore reboot;
    CHECK(reboot.Load(&disk, registry) == AccountStore::Ready && reboot.Recovered());
    CHECK(!registry.Credentials(1001));
    return 0;
}

TEST(account_store_corruption_is_not_first_boot)
{
    MemoryAccounts disk;
    AccountStore store;
    AccountRegistry registry;
    disk.slots[1][77] = 1;
    CHECK(store.Load(&disk, registry) == AccountStore::Broken);
    disk.slots[0][99] = 1;
    CHECK(store.Load(&disk, registry) == AccountStore::Broken);
    disk.readError = true;
    CHECK(store.Load(&disk, registry) == AccountStore::Broken);
    return 0;
}

TEST(account_store_flush_failure_freezes_updates)
{
    MemoryAccounts disk;
    AccountStore store;
    AccountRegistry registry;
    CHECK(store.Load(&disk, registry) == AccountStore::Empty);
    Administrator(registry);
    disk.flushError = true;
    CHECK(!store.Commit(registry));
    CHECK(store.Status() == AccountStore::Broken);
    disk.flushError = false;
    CHECK(!store.Commit(registry));
    AccountStore reboot;
    CHECK(reboot.Load(&disk, registry) == AccountStore::Ready);
    return 0;
}

TEST(account_store_rejects_invalid_schema)
{
    AccountRegistry registry;
    Administrator(registry);
    AccountData data = registry.Data();
    CHECK(AccountRegistry::Validate(data));
    data.userCount = 33;
    CHECK(!AccountRegistry::Validate(data));
    data = registry.Data();
    data.users[1].credentials.uid = 0;
    CHECK(!AccountRegistry::Validate(data));
    data = registry.Data();
    data.passwords[0].iterations = 1;
    CHECK(!AccountRegistry::Validate(data));
    data = registry.Data();
    data.users[0].name[0] = 0;
    CHECK(!AccountRegistry::Validate(data));
    data = registry.Data();
    data.users[1].credentials.groupCount = 9;
    CHECK(!AccountRegistry::Validate(data));
    data = registry.Data();
    for (auto& c : data.users[0].name)
    {
        c = 'a';
    }

    CHECK(!AccountRegistry::Validate(data));
    return 0;
}

static bool Hex(const uint8_t* bytes, const char* expected)
{
    const char* digits = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i)
    {
        if (digits[bytes[i] >> 4] != expected[i * 2] || digits[bytes[i] & 15] != expected[i * 2 + 1])
        {
            return false;
        }
    }

    return true;
}

TEST(sha256_known_vectors)
{
    uint8_t hash[32];
    Sha256("", 0, hash);
    CHECK(Hex(hash, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    Sha256("abc", 3, hash);
    CHECK(Hex(hash, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    Sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hash);
    CHECK(Hex(hash, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    return 0;
}

TEST(pbkdf2_sha256_known_vectors)
{
    uint8_t hash[32];
    Pbkdf2Sha256("password", 8, (const uint8_t*)"salt", 4, 1, hash);
    CHECK(Hex(hash, "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
    Pbkdf2Sha256("password", 8, (const uint8_t*)"salt", 4, 2, hash);
    CHECK(Hex(hash, "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"));
    Pbkdf2Sha256("password", 8, (const uint8_t*)"salt", 4, 4096, hash);
    CHECK(Hex(hash, "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));
    uint8_t same[32];
    test_copy(same, hash, 32);
    CHECK(ConstantEqual(hash, same, 32));
    same[31] ^= 1;
    CHECK(!ConstantEqual(hash, same, 32));
    SecureErase(hash, sizeof(hash));
    CHECK(Hex(hash, "0000000000000000000000000000000000000000000000000000000000000000"));
    return 0;
}

TEST(account_store_sector_interruptions_and_readback_failure)
{
    for (unsigned cut = 0; cut <= 8192; cut += 512)
    {
        MemoryAccounts disk;
        AccountStore store;
        AccountRegistry registry;
        CHECK(store.Load(&disk, registry) == AccountStore::Empty);
        Administrator(registry);
        CHECK(store.Commit(registry));
        registry.CreateGroup("later");
        disk.tornWrite = cut;
        CHECK(!store.Commit(registry));
        AccountStore reboot;
        CHECK(reboot.Load(&disk, registry) == AccountStore::Ready);
        CHECK(AccountRegistry::Validate(registry.Data()));
        if (cut == 0)
        {
            CHECK(registry.Group(2) == nullptr);
        }
        else if (cut == 8192)
        {
            CHECK(registry.Group(2) && test_name_equal(registry.Group(2)->name, "later"));
        }
    }

    MemoryAccounts disk;
    AccountStore store;
    AccountRegistry registry;
    CHECK(store.Load(&disk, registry) == AccountStore::Empty);
    Administrator(registry);
    disk.corruptWrite = true;
    CHECK(!store.Commit(registry) && store.Status() == AccountStore::Broken);
    AccountStore reboot;
    CHECK(reboot.Load(&disk, registry) == AccountStore::Broken);
    return 0;
}

TEST(pbkdf2_sha256_long_password)
{
    char password[100];
    test_fill(password, 'x', sizeof(password));
    uint8_t hash[32];
    Pbkdf2Sha256(password, sizeof(password), (const uint8_t*)"salt", 4, 2, hash);
    CHECK(Hex(hash, "d43a18cd77bafc1a4b0c6025dbbf29c7e6d67acce6ad02a736d4a3003b6a3c26"));
    return 0;
}
