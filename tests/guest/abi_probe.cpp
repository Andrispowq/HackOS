#include "hackos/user.h"
extern "C" int context_probe();
int main()
{
    uint16_t cs;
    asm volatile("mov %%cs,%0" : "=r"(cs));
    bool ok = (cs & 3) == 3;
    ok = MakeSyscall(~0ULL,1,2,3,4,5) == HACKOS_ERROR && ok;
    for (uint64_t nr = 1; nr <= 13; ++nr) ok = MakeSyscall(nr,0,0,0,0,0) == HACKOS_ERROR && ok;
    ok = syscall_power(0) == HACKOS_DENIED && ok;
    ok = syscall_power(1) == HACKOS_DENIED && ok;
    ok = syscall_input() == 0 && ok;
    ok = syscall_spawn_foreground("~/USR/BIN/TEST",0,nullptr) == HACKOS_DENIED && ok;
    ok = syscall_console(1) == HACKOS_DENIED && ok;
    ok = syscall_kill(2) == HACKOS_DENIED && ok;
    ok = syscall_clock((HackosClock*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_clock((HackosClock*)0x400000000000) == HACKOS_ERROR && ok;
    ok = syscall_process_info(0,(HackosProcessInfo*)0x4000001fffff) == HACKOS_ERROR && ok;
    ok = syscall_open((const char*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_spawn((const char*)0x4000001fffff) == HACKOS_ERROR && ok;
    char unterminated[256]; for (char& c : unterminated) c = 'x';
    ok = syscall_open(unterminated) == HACKOS_ERROR && ok;
    const char path[] = "~/USR/HELLO.TXT";
    HackosFileInfo info;
    ok = syscall_stat(path,&info) == 0 && info.size > 20 && info.size < 512 && ok;
    ok = syscall_stat(path,(HackosFileInfo*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_readdir("~/USR",0,(HackosFileInfo*)0x400000000000) == HACKOS_ERROR && ok;
    uint64_t h = syscall_open(path);
    ok = h != HACKOS_ERROR && ok;
    char buffer[512];
    ok = syscall_read(h,(void*)0x100000,8) == HACKOS_ERROR && ok;
    ok = syscall_read(h,(void*)0x400000000000,8) == HACKOS_ERROR && ok;
    ok = syscall_read(h,(void*)0x4000001fffff,8) == HACKOS_ERROR && ok;
    ok = syscall_read(h,buffer,513) == HACKOS_ERROR && ok;
    uint64_t count = syscall_read(h,buffer,512);
    ok = count == info.size && buffer[0] == 'T' && buffer[1] == 'h' && ok;
    ok = syscall_read(h,buffer,512) == 0 && ok;
    ok = syscall_close(h) == 0 && ok;
    ok = syscall_close(h) == HACKOS_ERROR && ok;
    uint64_t replacement = syscall_open(path);
    ok = replacement != h && replacement != HACKOS_ERROR && ok;
    ok = syscall_read(h,buffer,1) == HACKOS_ERROR && ok;
    syscall_close(replacement);
    // The shell deliberately owns one open handle. Guessing another process's
    // token must never work, even with a valid destination.
    ok = syscall_read(1,buffer,1) == HACKOS_ERROR && ok;
    ok = syscall_read(2,buffer,1) == HACKOS_ERROR && ok;
    ok = syscall_close(2) == HACKOS_ERROR && ok;
    uint64_t handles[8];
    for (auto& handle : handles) { handle = syscall_open(path); ok = handle != HACKOS_ERROR && ok; }
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
