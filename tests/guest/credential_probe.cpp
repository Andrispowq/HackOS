#include "hackos/user.h"
using namespace User;
static const char* program = "~/USR/BIN/CREDPROBE";

static bool Check(bool condition, const char* name)
{
    if (!condition)
    {
        Print("PROBE_FAIL: ");
        Print(name);
        Print("\n");
    }

    return condition;
}

static uint64_t Parse(const char* text)
{
    uint64_t value = 0;
    while (*text)
    {
        value = value * 10 + *text++ - '0';
    }

    return value;
}

static void Decimal(char* text, uint64_t value)
{
    if (value >= 10)
    {
        Decimal(text, value / 10);
        text += Length(text);
    }

    text[0] = '0' + value % 10;
    text[1] = 0;
}

static bool Wait(uint64_t pid, int64_t expected)
{
    int64_t status = -1;
    return pid != HACKOS_ERROR && pid != HACKOS_DENIED && syscall_wait(pid, &status, 0) == pid && status == expected;
}

int main(int argc, char** argv)
{
    HackosCredentials identity;
    if (!Check(syscall_identity(&identity) == 0, "read identity"))
    {
        return 1;
    }

    if (argc > 1 && Equal(argv[1], "hold"))
    {
        for (;;)
        {
            syscall_yield();
        }
    }

    if (argc > 1 && Equal(argv[1], "handoff-leaf"))
    {
        for (unsigned i = 0; i < 30; ++i)
        {
            syscall_yield();
        }

        return Check(syscall_input() == 0, "orphan retains delegated input") ? 0 : 1;
    }

    if (argc > 1 && Equal(argv[1], "handoff-exit"))
    {
        const char* leaf[] = {program, "handoff-leaf"};
        uint64_t child = syscall_spawn_foreground(program, 2, leaf);
        return Check(child != HACKOS_ERROR && child != HACKOS_DENIED, "delegate before launcher exit") ? 0 : 1;
    }

    if (argc > 1 && Equal(argv[1], "snapshot"))
    {
        HackosUserInfo user;
        do
        {
            syscall_yield();
            if (syscall_user_info(1, &user) != 1)
            {
                return 1;
            }
        } while (user.credentials.groupCount != 2);

        return Check(identity.uid == 1000 && identity.groupCount == 1 && syscall_identity(&identity) == 0 &&
                         identity.groupCount == 1,
                     "live process credential snapshot")
                   ? 0
                   : 1;
    }

    if ((argc > 1 && Equal(argv[1], "inherit")) || (argc == 1 && identity.uid == 1000))
    {
        const char* args[] = {program, "hold"};
        return Check(identity.uid == 1000 && identity.gid == 1000 && identity.groupCount == 2 &&
                         identity.groups[0] == 0 && identity.reserved == 0 && syscall_input() == HACKOS_DENIED &&
                         syscall_spawn_foreground(program, 2, args) == HACKOS_DENIED,
                     "grandchild inheritance and background input")
                   ? 0
                   : 1;
    }

    bool ok = true;
    const char* hold[] = {program, "hold"};
    if (argc == 4 && Equal(argv[1], "guest"))
    {
        ok = Check(identity.uid == 1000 && identity.gid == 1000 && identity.groupCount == 2 && identity.groups[0] == 0,
                   "guest credentials including root group") &&
             ok;
        identity.uid = 0;
        ok = Check(syscall_identity(&identity) == 0 && identity.uid == 1000, "copied identity is not authority") && ok;
        ok = Check(syscall_power(0) == HACKOS_DENIED && syscall_power(1) == HACKOS_DENIED, "guest power denied") && ok;
        ok = Check(syscall_user_create("forged", 0) == HACKOS_DENIED &&
                       syscall_group_create("forged") == HACKOS_DENIED &&
                       syscall_user_add_group(1000, 0) == HACKOS_DENIED,
                   "guest account changes denied") &&
             ok;
        ok = Check(syscall_spawn_as(program, 2, hold, 0, 0) == HACKOS_DENIED &&
                       syscall_spawn_as(program, 2, hold, 1000, 0) == HACKOS_DENIED,
                   "guest identity selection denied") &&
             ok;
        ok = Check(syscall_kill(Parse(argv[2])) == HACKOS_DENIED && syscall_kill(Parse(argv[3])) == HACKOS_DENIED &&
                       syscall_kill(1) == HACKOS_DENIED && syscall_kill(2) == HACKOS_DENIED,
                   "foreign and protected processes denied") &&
             ok;
        const char* inherit[] = {"shutdown", "inherit"}; // argv[0] must not grant authority
        ok = Check(Wait(syscall_spawn_args(program, 2, inherit), 0), "ordinary spawn inherits credentials") && ok;
        ok = Check(Wait(syscall_spawn(program), 0), "single-argument spawn inherits credentials") && ok;
        ok = Check(syscall_wait(Parse(argv[2]), nullptr, 0) == HACKOS_ERROR,
                   "ownership does not grant foreign wait rights") &&
             ok;
        uint64_t child = syscall_spawn_args(program, 2, hold);
        HackosProcessInfo info;
        bool owned = false;
        for (uint64_t i = 0; syscall_process_info(i, &info) == 1; ++i)
        {
            if (info.pid == child)
            {
                owned = info.uid == 1000 && info.gid == 1000 && info.parentPid != 0;
            }
        }

        ok = Check(owned && syscall_kill(child) == 0 && Wait(child, 137), "same-owner kill and wait") && ok;
        ok = Check(syscall_console(1) == 0, "unprivileged terminal clear") && ok;
        const char* denied[] = {"~/USR/BIN/RESTART"};
        ok = Check(Wait(syscall_spawn_foreground(denied[0], 1, denied), 1), "canonical restart stays unprivileged") &&
             ok;
        ok = Check(syscall_input() == 0, "nested terminal return") && ok;
        return ok ? 0 : 1;
    }

    ok = Check(identity.uid == 0 && identity.gid == 0, "root inheritance") && ok;
    ok = Check(syscall_power(2) == HACKOS_ERROR, "root reaches power operation validation") && ok;
    ok = Check(syscall_identity((HackosCredentials*)0x100000) == HACKOS_ERROR &&
                   syscall_identity((HackosCredentials*)0x400000000000) == HACKOS_ERROR &&
                   syscall_identity((HackosCredentials*)0x4000001fffff) == HACKOS_ERROR,
               "identity copy bounds") &&
         ok;
    ok = Check(syscall_user_info(0, (HackosUserInfo*)0x100000) == HACKOS_ERROR &&
                   syscall_group_info(0, (HackosGroupInfo*)0x4000001fffff) == HACKOS_ERROR &&
                   syscall_user_info(~0ULL, nullptr) == 0 && syscall_group_info(~0ULL, nullptr) == 0,
               "registry output bounds") &&
         ok;
    char unterminated[HACKOS_ACCOUNT_NAME];
    for (auto& c : unterminated)
    {
        c = 'a';
    }

    ok = Check(syscall_user_create((const char*)0x100000, 0) == HACKOS_ERROR &&
                   syscall_group_create(unterminated) == HACKOS_ERROR &&
                   syscall_user_create("bad:user", 1000) == HACKOS_ERROR &&
                   syscall_user_create("root", 1000) == HACKOS_ERROR &&
                   syscall_user_create("badgid", 1ULL << 32) == HACKOS_ERROR,
               "account input validation") &&
         ok;
    ok = Check(syscall_spawn_as(program, 2, hold, 1ULL << 32, 0) == HACKOS_ERROR &&
                   syscall_spawn_as(program, 2, hold, 0, 2) == HACKOS_ERROR &&
                   syscall_spawn_as((const char*)0x100000, 2, hold, 0, 0) == HACKOS_ERROR &&
                   syscall_spawn_as(program, 2, (const char* const*)0x100000, 0, 0) == HACKOS_ERROR,
               "spawn identity input validation") &&
         ok;
    uint64_t group = syscall_group_create("qa-test");
    uint64_t other = syscall_user_create("other-test", 1000);
    ok = Check(group != HACKOS_ERROR && other != HACKOS_ERROR && syscall_user_add_group(1000, 0) == 0,
               "root account administration") &&
         ok;
    const char* snapshot[] = {program, "snapshot"};
    uint64_t snapshotPid = syscall_spawn_as(program, 2, snapshot, 1000, 0);
    ok = Check(syscall_user_add_group(1000, group) == 0 && Wait(snapshotPid, 0),
               "registry updates are not retroactive") &&
         ok;
    uint64_t rootPid = syscall_spawn_args(program, 2, hold);
    uint64_t foreignPid = syscall_spawn_as(program, 2, hold, other, 0);
    char rootText[21], foreignText[21];
    Decimal(rootText, rootPid);
    Decimal(foreignText, foreignPid);
    const char* guest[] = {program, "guest", rootText, foreignText};
    ok = Check(Wait(syscall_spawn_as(program, 4, guest, 1000, HACKOS_SPAWN_FOREGROUND), 0), "guest permission probe") &&
         ok;
    ok = Check(syscall_kill(rootPid) == 0 && Wait(rootPid, 137) && syscall_kill(foreignPid) == 0 &&
                   Wait(foreignPid, 137),
               "root cross-owner kill") &&
         ok;
    ok =
        Check(syscall_identity(&identity) == 0 && identity.uid == 0 && identity.groupCount == 0 && syscall_input() == 0,
              "parent credentials and terminal unchanged") &&
        ok;
    const char* orphan[] = {program, "handoff-exit"};
    ok = Check(Wait(syscall_spawn_foreground(program, 2, orphan), 0), "foreground launcher exits before child") && ok;
    while (syscall_input() == HACKOS_DENIED)
    {
        syscall_yield();
    }

    ok = Check(syscall_input() == 0, "orphan returns input to surviving ancestor") && ok;
    Print(ok ? "CREDENTIALS_OK\n" : "PROBE_FAIL: credentials\n");
    return ok ? 0 : 1;
}
