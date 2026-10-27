#include "hackos/user.h"

int main(int argc, char** argv)
{
    bool ok = argc == 1 && argv && !argv[argc];
    const char* path = "~/USR/BIN/ARGPROBE";
    const char* args[] = {path, "quick"};
    int64_t status = -1;
    ok = syscall_wait(0, &status, 0) == HACKOS_ERROR && ok;
    ok = syscall_wait(2, &status, 0) == HACKOS_ERROR && ok;
    ok = syscall_spawn_args(path, 0, args) == HACKOS_ERROR && ok;
    ok = syscall_spawn_args(path, 17, args) == HACKOS_ERROR && ok;
    ok = syscall_spawn_args(path, 2, (const char* const*)0x100000) == HACKOS_ERROR && ok;
    const char* invalid[] = {path, (const char*)0x100000};
    ok = syscall_spawn_args(path, 2, invalid) == HACKOS_ERROR && ok;
    char huge[1025];
    for (unsigned i = 0; i < 1024; ++i)
    {
        huge[i] = 'x';
    }

    huge[1024] = 0;
    invalid[1] = huge;
    ok = syscall_spawn_args(path, 2, invalid) == HACKOS_ERROR && ok;
    uint64_t pid = syscall_spawn_args(path, 2, args);
    ok = pid != HACKOS_ERROR && ok;
    ok = syscall_wait(pid, &status, 2) == HACKOS_ERROR && ok;
    ok = syscall_wait(pid, (int64_t*)0x400000000000, 0) == HACKOS_ERROR && ok;
    ok = syscall_wait(pid, (int64_t*)0x4000001fffff, 0) == HACKOS_ERROR && ok;
    // Give idle time to reclaim the child before collecting its exit status.
    for (unsigned i = 0; i < 30; ++i)
    {
        syscall_yield();
    }

    ok = syscall_wait(pid, &status, 0) == pid && status == 23 && ok;
    ok = syscall_wait(pid, &status, 0) == HACKOS_ERROR && ok;
    char value[] = "original";
    const char* copy[] = {path, "copy", value};
    pid = syscall_spawn_args(path, 3, copy);
    value[0] = 'X';
    uint64_t polled = syscall_wait(pid, &status, HACKOS_WAIT_NOHANG);
    if (!polled)
    {
        ok = syscall_wait(pid, &status, 0) == pid && ok;
    }
    else
    {
        ok = polled == pid && ok;
    }

    ok = status == 31 && ok;
    args[1] = "fault";
    pid = syscall_spawn_args(path, 2, args);
    ok = syscall_wait(pid, &status, 0) == pid && status == 134 && ok;
    pid = syscall_spawn(path);
    ok = syscall_wait(pid, &status, 0) == pid && status == 19 && ok;
    args[1] = "orphan";
    pid = syscall_spawn_args(path, 2, args);
    ok = syscall_wait(pid, &status, 0) == pid && status == 41 && ok;
    User::Print(ok ? "WAIT_TEST_OK\n" : "PROBE_FAIL: wait contract\n");
    return ok ? 0 : 1;
}
