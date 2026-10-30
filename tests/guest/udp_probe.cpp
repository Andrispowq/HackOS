#include "hackos/user.h"
#include "hackos/stdio.h"

static bool Valid(uint64_t handle)
{
    return handle && !(handle >> 63);
}

static bool Receive(uint64_t handle, HackosDatagram& record)
{
    for (unsigned i = 0; i < 150; ++i)
    {
        uint64_t status = syscall_udp(HACKOS_UDP_STATUS, handle, &record);
        if (status) return false;
        if (record.queued) return true;
        syscall_sleep(20);
    }
    return false;
}

int main(int argc, char** argv)
{
    HackosDatagram record = {};
    record.address[0] = 10;
    record.address[2] = 2;
    record.address[3] = 2;
    record.peerPort = 7777;
    if (argc == 2)
    {
        uint64_t parent = 0;
        for (const char* p = argv[1]; *p; ++p)
        {
            parent = parent * 10 + (*p - '0');
        }
        bool ok = syscall_udp(HACKOS_UDP_CLOSE, parent, nullptr) == HACKOS_NOT_FOUND;
        ok = syscall_udp(HACKOS_UDP_STATUS, parent, &record) == HACKOS_NOT_FOUND && ok;
        for (unsigned i = 0; i < 4; ++i)
        {
            ok = Valid(syscall_udp(HACKOS_UDP_OPEN, 0, &record)) && ok;
        }
        return ok ? 0 : 1; // Reclamation must release all four ports and queues.
    }
    bool ok = syscall_udp(HACKOS_UDP_OPEN, 0, (HackosDatagram*)0x100000) == HACKOS_ERROR;
    ok = syscall_udp(HACKOS_UDP_OPEN, 0, (HackosDatagram*)0x4000001fffff) == HACKOS_ERROR && ok;
    record.localPort = 53;
    ok = syscall_udp(HACKOS_UDP_OPEN, 0, &record) == HACKOS_ERROR && ok;
    record.localPort = 49152;
    ok = syscall_udp(HACKOS_UDP_OPEN, 0, &record) == HACKOS_ERROR && ok;
    record.localPort = 8000;
    uint64_t handles[4];
    handles[0] = syscall_udp(HACKOS_UDP_OPEN, 0, &record);
    ok = Valid(handles[0]) && ok;
    ok = syscall_udp(HACKOS_UDP_OPEN, 0, &record) == HACKOS_RETRY && ok;
    record.localPort = 0;
    for (unsigned i = 1; i < 4; ++i)
    {
        handles[i] = syscall_udp(HACKOS_UDP_OPEN, 0, &record);
        ok = Valid(handles[i]) && ok;
    }
    ok = syscall_udp(HACKOS_UDP_OPEN, 0, &record) == HACKOS_RETRY && ok;
    char parent[24];
    snprintf(parent, sizeof(parent), "%lu", handles[0]);
    for (unsigned i = 0; i < 10; ++i)
    {
        const char* args[] = {"udpprobe", parent};
        uint64_t child = syscall_spawn_args("/usr/bin/udpprobe", 2, args);
        int64_t status = -1;
        ok = child != HACKOS_ERROR && syscall_wait(child, &status, 0) == child && status == 0 && ok;
        syscall_sleep(20);
    }
    uint64_t handle = handles[0];
    ok = syscall_udp(999, handle, nullptr) == HACKOS_ERROR && ok;
    ok = syscall_udp(HACKOS_UDP_STATUS, handle, (HackosDatagram*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_udp(HACKOS_UDP_SEND, handle, (HackosDatagram*)0x100000) == HACKOS_ERROR && ok;
    record.size = 513;
    ok = syscall_udp(HACKOS_UDP_SEND, handle, &record) == HACKOS_ERROR && ok;
    record.size = 512;
    for (unsigned i = 0; i < 512; ++i)
    {
        record.data[i] = i;
    }
    ok = syscall_udp(HACKOS_UDP_SEND, handle, &record) == 0 && ok;
    ok = Receive(handle, record) && ok;
    ok = syscall_udp(HACKOS_UDP_RECEIVE, handle, (HackosDatagram*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_udp(HACKOS_UDP_RECEIVE, handle, (HackosDatagram*)0x400000000000) == HACKOS_ERROR && ok;
    struct Guarded { uint64_t before; HackosDatagram value; uint64_t after; } guarded = {};
    guarded.before = guarded.after = 0x123456789abcdef0ULL;
    ok = syscall_udp(HACKOS_UDP_RECEIVE, handle, &guarded.value) == 0 && ok;
    ok = guarded.before == guarded.after && guarded.before == 0x123456789abcdef0ULL && ok;
    ok = guarded.value.size == 512 && guarded.value.localPort == 8000 && guarded.value.peerPort == 7777 && ok;
    for (unsigned i = 0; i < 512; ++i)
    {
        ok = guarded.value.data[i] == (uint8_t)i && ok;
    }
    ok = syscall_udp(HACKOS_UDP_RECEIVE, handle, &record) == HACKOS_RETRY && ok;
    record.size = 0;
    ok = syscall_udp(HACKOS_UDP_SEND, handle, &record) == 0 && Receive(handle, record) && ok;
    ok = syscall_udp(HACKOS_UDP_RECEIVE, handle, &record) == 0 && record.size == 0 && ok;
    const char* burst = "burst";
    record.size = 5;
    for (unsigned i = 0; i < 5; ++i)
    {
        record.data[i] = burst[i];
    }
    ok = syscall_udp(HACKOS_UDP_SEND, handle, &record) == 0 && Receive(handle, record) && ok;
    syscall_sleep(500);
    ok = syscall_udp(HACKOS_UDP_STATUS, handle, &record) == 0 && record.queued == 4 && record.dropped == 3 && ok;
    for (unsigned i = 0; i < 4; ++i)
    {
        ok = syscall_udp(HACKOS_UDP_RECEIVE, handle, &record) == 0 && record.size == 1 && record.data[0] == i && ok;
        for (unsigned j = 1; j < 512; ++j)
        {
            ok = record.data[j] == 0 && ok;
        }
    }
    for (unsigned i = 0; i < 4; ++i)
    {
        ok = syscall_udp(HACKOS_UDP_CLOSE, handles[i], nullptr) == 0 && ok;
    }
    ok = syscall_udp(HACKOS_UDP_STATUS, handle, &record) == HACKOS_NOT_FOUND && ok;
    record.localPort = 8000;
    uint64_t replacement = syscall_udp(HACKOS_UDP_OPEN, 0, &record);
    ok = Valid(replacement) && replacement != handle && ok;
    ok = syscall_udp(HACKOS_UDP_CLOSE, replacement, nullptr) == 0 && ok;
    User::Print(ok ? "UDP_ABI_OK\n" : "UDP_ABI_FAIL\n");
    return ok ? 0 : 1;
}
