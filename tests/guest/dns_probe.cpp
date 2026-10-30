#include "hackos/user.h"

int main(int argc, char**)
{
    HackosResolve echo = {};
    const char* name = "silent.test";
    for (unsigned i = 0; name[i]; ++i)
    {
        echo.name[i] = name[i];
    }
    bool ok = syscall_resolve(HACKOS_RESOLVE_QUERY, &echo) == HACKOS_NOT_FOUND;
    if (argc > 1)
    {
        ok = syscall_resolve(HACKOS_RESOLVE_START, &echo) == 0 && ok;
        return ok ? 0 : 1; // Deliberately exit with a live request.
    }
    ok = syscall_resolve(999, &echo) == HACKOS_ERROR && ok;
    ok = syscall_resolve(HACKOS_RESOLVE_START, (HackosResolve*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_resolve(HACKOS_RESOLVE_START, (HackosResolve*)0x4000001fffff) == HACKOS_ERROR && ok;
    echo.name[0] = '-';
    ok = syscall_resolve(HACKOS_RESOLVE_START, &echo) == HACKOS_ERROR && ok;
    echo.name[0] = 's';
    ok = syscall_resolve(HACKOS_RESOLVE_START, &echo) == 0 && ok;
    ok = syscall_resolve(HACKOS_RESOLVE_START, &echo) == HACKOS_RETRY && ok;
    ok = syscall_resolve(HACKOS_RESOLVE_QUERY, (HackosResolve*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_resolve(HACKOS_RESOLVE_QUERY, (HackosResolve*)0x400000000000) == HACKOS_ERROR && ok;
    // Child processes cannot query the parent's request and leave no slot leaks.
    for (unsigned i = 0; i < 10; ++i)
    {
        const char* args[] = {"dnsprobe", "child"};
        uint64_t pid = syscall_spawn_args("/usr/bin/dnsprobe", 2, args);
        int64_t status = -1;
        ok = pid != HACKOS_ERROR && syscall_wait(pid, &status, 0) == pid && status == 0 && ok;
        syscall_sleep(20);
    }
    ok = syscall_resolve(HACKOS_RESOLVE_CANCEL, nullptr) == 0 && ok;
    ok = syscall_resolve(HACKOS_RESOLVE_QUERY, &echo) == HACKOS_NOT_FOUND && ok;
    User::Print(ok ? "DNS_ABI_OK\n" : "DNS_ABI_FAIL\n");
    return ok ? 0 : 1;
}
