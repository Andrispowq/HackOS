#include "hackos/user.h"

int main(int argc, char**)
{
    HackosEcho echo = {{10, 0, 2, 2}, 0, 0, 0, 0};
    bool ok = syscall_echo(HACKOS_ECHO_QUERY, &echo) == HACKOS_NOT_FOUND;
    if (argc > 1)
    {
        ok = syscall_echo(HACKOS_ECHO_START, &echo) == 0 && ok;
        return ok ? 0 : 1; // Deliberately exit with a live request.
    }
    ok = syscall_echo(999, &echo) == HACKOS_ERROR && ok;
    ok = syscall_echo(HACKOS_ECHO_START, (HackosEcho*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_echo(HACKOS_ECHO_START, (HackosEcho*)0x4000001fffff) == HACKOS_ERROR && ok;
    echo.address[3] = 99;
    ok = syscall_echo(HACKOS_ECHO_START, &echo) == HACKOS_ERROR && ok;
    echo.address[3] = 2;
    ok = syscall_echo(HACKOS_ECHO_START, &echo) == 0 && ok;
    ok = syscall_echo(HACKOS_ECHO_START, &echo) == HACKOS_RETRY && ok;
    ok = syscall_echo(HACKOS_ECHO_QUERY, (HackosEcho*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_echo(HACKOS_ECHO_QUERY, (HackosEcho*)0x400000000000) == HACKOS_ERROR && ok;
    // Child processes cannot query the parent's request and leave no slot leaks.
    for (unsigned i = 0; i < 10; ++i)
    {
        const char* args[] = {"pingprobe", "child"};
        uint64_t pid = syscall_spawn_args("/usr/bin/pingprobe", 2, args);
        int64_t status = -1;
        ok = pid != HACKOS_ERROR && syscall_wait(pid, &status, 0) == pid && status == 0 && ok;
        syscall_sleep(20);
    }
    ok = syscall_echo(HACKOS_ECHO_CANCEL, nullptr) == 0 && ok;
    ok = syscall_echo(HACKOS_ECHO_QUERY, &echo) == HACKOS_NOT_FOUND && ok;
    User::Print(ok ? "PING_ABI_OK\n" : "PING_ABI_FAIL\n");
    return ok ? 0 : 1;
}
