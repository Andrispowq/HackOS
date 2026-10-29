#include "support.hpp"
#include "proc/user/accounts.h"

TEST(account_defaults)
{
    AccountRegistry registry;
    registry.Initialise();
    CHECK(registry.User(0)->credentials.uid == 0);
    CHECK(test_name_equal(registry.User(0)->name, "root"));
    CHECK(registry.Credentials(1000)->gid == 1000);
    CHECK(registry.Credentials(0)->groupCount == 0);
    CHECK(!registry.User(2) && !registry.User(~0ULL));
    CHECK(!registry.Group(2) && !registry.Group(~0ULL));
    CHECK(!registry.Credentials(1) && !registry.Credentials(1ULL << 32));
    return 0;
}

TEST(account_names_and_stable_ids)
{
    AccountRegistry registry;
    registry.Initialise();
    const char* invalid[] = {
        "", "Root", "1user", "a b", "a:b", "a/b", "a\nb", "a.b", "abcdefghijklmnopqrstuvwxyzabcdef", nullptr};
    for (const char* name : invalid)
    {
        CHECK(registry.CreateUser(name, 1000) == HACKOS_ERROR);
        CHECK(registry.CreateGroup(name) == HACKOS_ERROR);
    }

    CHECK(registry.CreateUser("root", 1000) == HACKOS_ERROR);
    CHECK(registry.CreateGroup("root") == HACKOS_ERROR);
    CHECK(registry.CreateUser("alice", 7) == HACKOS_ERROR);
    CHECK(registry.CreateUser("alice", 1ULL << 32) == HACKOS_ERROR);
    CHECK(registry.CreateGroup("dev-team_1") == 1001);
    CHECK(registry.CreateUser("alice", 1001) == 1001);
    CHECK(registry.CreateUser("alice", 1000) == HACKOS_ERROR);
    CHECK(registry.CreateUser("abcdefghijklmnopqrstuvwxyzabcde", 1000) == 1002);
    CHECK(registry.Credentials(1001)->gid == 1001);
    CHECK(registry.Credentials(1002)->gid == 1000);
    return 0;
}

TEST(account_capacity)
{
    AccountRegistry registry;
    registry.Initialise();
    for (unsigned i = 0; i < 30; ++i)
    {
        char name[] = {'a', (char)('a' + i / 26), (char)('a' + i % 26), 0};
        CHECK(registry.CreateGroup(name) == 1001 + i);
        CHECK(registry.CreateUser(name, 1001 + i) == 1001 + i);
    }

    CHECK(registry.CreateGroup("overflow") == HACKOS_ERROR);
    CHECK(registry.CreateUser("overflow", 0) == HACKOS_ERROR);
    CHECK(registry.User(31)->credentials.uid == 1030);
    CHECK(!registry.User(32) && !registry.Group(32));
    return 0;
}

TEST(account_memberships_and_snapshot)
{
    AccountRegistry registry;
    registry.Initialise();
    CHECK(registry.AddGroup(1000, 1000) == 0);
    CHECK(registry.AddGroup(1000, 0) == 0);
    CHECK(registry.AddGroup(1000, 0) == 0);
    CHECK(registry.AddGroup(1ULL << 32, 0) == HACKOS_ERROR);
    CHECK(registry.AddGroup(1000, 1ULL << 32) == HACKOS_ERROR);
    HackosCredentials snapshot = *registry.Credentials(1000);
    CHECK(snapshot.groupCount == 1 && snapshot.groups[0] == 0 && snapshot.uid == 1000);
    for (unsigned i = 0; i < 8; ++i)
    {
        char name[] = {'g', (char)('a' + i), 0};
        uint64_t gid = registry.CreateGroup(name);
        CHECK(registry.AddGroup(1000, gid) == (i < 7 ? 0 : HACKOS_ERROR));
    }

    CHECK(snapshot.groupCount == 1 && snapshot.groups[1] == 0);
    CHECK(registry.Credentials(1000)->groupCount == 8);
    CHECK(registry.Credentials(1000)->reserved == 0);
    CHECK(registry.AddGroup(1000, 0) == 0);
    return 0;
}

TEST(account_edit_preserves_ids_and_valid_store)
{
    AccountRegistry registry;
    registry.Initialise();
    PasswordRecord password = {};
    password.enabled = 1;
    password.iterations = 600000;
    CHECK(registry.Setup("admin", password));
    CHECK(registry.CreateGroup("staff") == 1001);
    CHECK(registry.CreateUser("alice", 1000) == 1001);
    CHECK(registry.Edit(HACKOS_ACCOUNT_RENAME_USER, 1001, 0, "bob") == 0);
    CHECK(registry.FindUser("alice") == -1 && registry.FindUser("bob") == 2);
    CHECK(registry.Credentials(1001)->uid == 1001);
    CHECK(registry.Edit(HACKOS_ACCOUNT_RENAME_USER, 1001, 0, "admin") == HACKOS_ERROR);
    CHECK(registry.Edit(HACKOS_ACCOUNT_RENAME_GROUP, 1001, 0, "dev") == 0);
    CHECK(registry.Edit(HACKOS_ACCOUNT_RENAME_GROUP, 1001, 0, "users") == HACKOS_ERROR);
    CHECK(registry.Edit(HACKOS_ACCOUNT_RENAME_USER, 1ULL << 32, 0, "bad") == HACKOS_ERROR);
    CHECK(registry.Edit(HACKOS_ACCOUNT_RENAME_USER, 1001, 0, "Bad") == HACKOS_ERROR);
    CHECK(registry.AddGroup(1001, 1001) == 0);
    CHECK(registry.Edit(HACKOS_ACCOUNT_PRIMARY_GROUP, 1001, 1001, nullptr) == 0);
    CHECK(registry.Credentials(1001)->gid == 1001 && registry.Credentials(1001)->groupCount == 0);
    CHECK(registry.Edit(HACKOS_ACCOUNT_REMOVE_GROUP, 1001, 1001, nullptr) == HACKOS_ERROR);
    CHECK(registry.AddGroup(1001, 1000) == 0);
    CHECK(registry.Edit(HACKOS_ACCOUNT_REMOVE_GROUP, 1001, 1000, nullptr) == 0);
    CHECK(registry.Credentials(1001)->groupCount == 0);
    CHECK(registry.Edit(HACKOS_ACCOUNT_PRIMARY_GROUP, 0, 1000, nullptr) == HACKOS_ERROR);
    CHECK(registry.Edit(99, 1001, 1000, nullptr) == HACKOS_ERROR);
    CHECK(AccountRegistry::Validate(registry.Data()));
    return 0;
}
