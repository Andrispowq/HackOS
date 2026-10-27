#include "hackos/user.h"
extern "C" int context_probe();

int main()
{
    HackosCredentials credentials;
    if (syscall_identity(&credentials) != 0)
    {
        User::Print("PROBE_FAIL: credentials\n");
        return 1;
    }

    if (credentials.uid == 0)
    {
        const char* args[] = {"~/USR/BIN/ABIPROBE"};
        uint64_t child = syscall_spawn_as(args[0], 1, args, 1000, HACKOS_SPAWN_FOREGROUND);
        int64_t status = 1;
        if (child == HACKOS_ERROR || child == HACKOS_DENIED || syscall_wait(child, &status, 0) != child)
        {
            User::Print("PROBE_FAIL: unprivileged launch\n");
            return 1;
        }

        return (int)status;
    }

    uint16_t cs;
    asm volatile("mov %%cs,%0" : "=r"(cs));
    bool ok = (cs & 3) == 3;
    ok = MakeSyscall(~0ULL, 1, 2, 3, 4, 5) == HACKOS_ERROR && ok;
    for (uint64_t nr = 1; nr <= 13; ++nr)
    {
        ok = MakeSyscall(nr, 0, 0, 0, 0, 0) == HACKOS_ERROR && ok;
    }

    ok = syscall_power(0) == HACKOS_DENIED && ok;
    ok = syscall_power(1) == HACKOS_DENIED && ok;
    ok = syscall_input() == 0 && ok;
    ok = syscall_spawn_foreground("~/USR/BIN/TEST", 0, nullptr) == HACKOS_ERROR && ok;
    ok = syscall_console(2) == HACKOS_ERROR && ok;
    ok = syscall_kill(2) == HACKOS_DENIED && ok;
    ok = syscall_clock((HackosClock*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_clock((HackosClock*)0x400000000000) == HACKOS_ERROR && ok;
    ok = syscall_process_info(0, (HackosProcessInfo*)0x4000001fffff) == HACKOS_ERROR && ok;
    ok = syscall_open((const char*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_spawn((const char*)0x4000001fffff) == HACKOS_ERROR && ok;
    char unterminated[256];
    for (char& c : unterminated)
    {
        c = 'x';
    }

    ok = syscall_open(unterminated) == HACKOS_ERROR && ok;
    char cwd[256];
    HackosFileInfo infoForCwd;
    ok = syscall_getcwd((char*)0x100000, 256) == HACKOS_ERROR && ok;
    ok = syscall_getcwd(cwd, 1) == HACKOS_ERROR && ok;
    ok = syscall_chdir((const char*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_getcwd(cwd, sizeof(cwd)) == 0 && User::Equal(cwd, "~") && ok;
    const char* powerArgs[] = {"~/USR/BIN/SHUTDOWN"};
    uint64_t powerChild = syscall_spawn_args("/usr/bin/shutdown", 1, powerArgs);
    int64_t powerStatus = 0;
    ok = powerChild != HACKOS_ERROR && ok;
    ok = syscall_wait(powerChild, &powerStatus, 0) == powerChild && powerStatus == 1 && ok;
    ok = syscall_chdir("/usr") == 0 && ok;
    ok = syscall_getcwd(cwd, sizeof(cwd)) == 0 && User::Equal(cwd, "~/USR") && ok;
    ok = syscall_chdir("hello.txt") == HACKOS_ERROR && ok;
    ok = syscall_stat("HELLO.TXT", &infoForCwd) == 0 && ok;
    const char path[] = "~/USR/HELLO.TXT";
    HackosFileInfo info;
    ok = syscall_stat(path, &info) == 0 && info.size > 20 && info.size < 512 && ok;
    ok = syscall_stat(path, (HackosFileInfo*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_readdir("~/USR", 0, (HackosFileInfo*)0x400000000000) == HACKOS_ERROR && ok;
    uint64_t h = syscall_open(path);
    ok = h != HACKOS_ERROR && ok;
    char buffer[512];
    ok = syscall_read(h, (void*)0x100000, 8) == HACKOS_ERROR && ok;
    ok = syscall_read(h, (void*)0x400000000000, 8) == HACKOS_ERROR && ok;
    ok = syscall_read(h, (void*)0x4000001fffff, 8) == HACKOS_ERROR && ok;
    ok = syscall_read(h, buffer, 513) == HACKOS_ERROR && ok;
    uint64_t count = syscall_read(h, buffer, 512);
    ok = count == info.size && buffer[0] == 'T' && buffer[1] == 'h' && ok;
    ok = syscall_read(h, buffer, 512) == 0 && ok;
    ok = syscall_close(h) == 0 && ok;
    ok = syscall_close(h) == HACKOS_ERROR && ok;
    uint64_t replacement = syscall_open(path);
    ok = replacement != h && replacement != HACKOS_ERROR && ok;
    ok = syscall_read(h, buffer, 1) == HACKOS_ERROR && ok;
    syscall_close(replacement);
    // Closed tokens from earlier command programs must not become valid again.
    ok = syscall_read(1, buffer, 1) == HACKOS_ERROR && ok;
    ok = syscall_read(2, buffer, 1) == HACKOS_ERROR && ok;
    ok = syscall_close(2) == HACKOS_ERROR && ok;
    uint64_t handles[8];
    for (auto& handle : handles)
    {
        handle = syscall_open(path);
        ok = handle != HACKOS_ERROR && ok;
    }

    ok = syscall_open(path) == HACKOS_ERROR && ok;
    // Leave all eight open: task teardown must reclaim them.
    User::Print("PROBE_CONTEXT_BEGIN\n");
    HackosClock before, after;
    syscall_clock(&before);
    ok = context_probe() == 0 && ok;
    syscall_clock(&after);
    ok = after.ticks > before.ticks + 2 && ok;
    User::Print(ok ? "PROBE_OK\n" : "PROBE_FAIL\n");
    return ok ? 0 : 1;
}
