#include "hackos/user.h"

int main(int argc, char**)
{
    HackosTcp r = {};
    r.address[0] = 10;
    r.address[2] = 2;
    r.address[3] = 2;
    r.peerPort = 9999;
    bool ok = syscall_tcp(HACKOS_TCP_QUERY, &r) == HACKOS_NOT_FOUND;
    if (argc > 1)
    {
        syscall_sleep(120);
        ok = syscall_tcp(HACKOS_TCP_START, &r) == 0 && ok;
        syscall_sleep(20);
        return ok ? 0 : 1;
    }
    ok = syscall_tcp(99, &r) == HACKOS_ERROR && ok;
    ok = syscall_tcp(HACKOS_TCP_START, (HackosTcp*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_tcp(HACKOS_TCP_START, (HackosTcp*)0x4000001fffff) == HACKOS_ERROR && ok;
    r.size = 513;
    ok = syscall_tcp(HACKOS_TCP_START, &r) == HACKOS_ERROR && ok;
    r.size = 0;
    syscall_sleep(120);
    ok = syscall_tcp(HACKOS_TCP_START, &r) == 0 && ok;
    ok = syscall_tcp(HACKOS_TCP_START, &r) == HACKOS_RETRY && ok;
    ok = syscall_tcp(HACKOS_TCP_QUERY, (HackosTcp*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_tcp(HACKOS_TCP_QUERY, (HackosTcp*)0x4000001fffff) == HACKOS_ERROR && ok;
    for (unsigned i = 0; i < 10; ++i)
    {
        const char* args[] = {"tcpprobe", "child"};
        uint64_t pid = syscall_spawn_args("/usr/bin/tcpprobe", 2, args);
        int64_t status = -1;
        ok = pid != HACKOS_ERROR && syscall_wait(pid, &status, 0) == pid && status == 0 && ok;
    }
    syscall_tcp(HACKOS_TCP_CANCEL, nullptr);
    ok = syscall_tcp(HACKOS_TCP_QUERY, &r) == HACKOS_NOT_FOUND && ok;
    r = {};
    r.address[0] = 10;
    r.address[2] = 2;
    r.address[3] = 2;
    r.peerPort = 7778;
    r.size = 512;
    for (unsigned i = 0; i < 512; ++i)
    {
        r.data[i] = i;
    }
    syscall_sleep(120);
    ok = syscall_tcp(HACKOS_TCP_START, &r) == 0 && ok;
    uint64_t status;
    do
    {
        syscall_sleep(20);
        status = syscall_tcp(HACKOS_TCP_QUERY, &r);
    } while (!status && r.state == HACKOS_TCP_PENDING);
    ok = !status && r.state == HACKOS_TCP_COMPLETE && r.received == 512 && ok;
    for (unsigned i = 0; i < 512; ++i)
    {
        ok = r.data[i] == (uint8_t)i && ok;
    }
    syscall_tcp(HACKOS_TCP_CANCEL, nullptr);
    User::Print(ok ? "TCP_ABI_OK\n" : "TCP_ABI_FAIL\n");
    return ok ? 0 : 1;
}
