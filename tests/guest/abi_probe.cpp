#include "stdint.h"
#include "memory.h"
#include "stdio.h"
#include "file/file.h"
#include "kernel_interface/syscall.h"

extern "C" int context_probe();
extern "C" volatile uint64_t guest_switches;
int main(int argc, char** argv)
{
    bool ok = argc == 1 && argv && argv[0] && !argv[1];
    ok = MakeSyscall(~0ULL, 1, 2, 3, 4, 5) == ~0ULL && ok;
    auto file = (ActiveFile*)syscall_fopen("fs0:/USR/HELLO.TXT", 0);
    if (!file) { printf("PROBE_FAIL: open\n"); return 1; }
    uint64_t size = file->GetSize();
    ok = ok && size > 20 && size < 512 && file->GetName()[0] && file->GetPath()[0];
    ok = ok && !(file->GetAttributes() & 16) && file->seek_position == 0 && file->fs_index == 0;
    auto buffer = (char*)malloc(size + 1);
    ok = ok && buffer;
    if (buffer)
    {
        auto read = syscall_fread(buffer, 1, size, file);
        buffer[size] = 0;
        ok = ok && read == 0 && buffer[0] == 'T' && buffer[1] == 'h';
        free(buffer);
    }
    syscall_fclose(file);
    // The formatter consumes a floating argument through a real int 0x80 call.
    printf("PROBE_FLOAT=%f\n", 1.25);
    printf("PROBE_CONTEXT_BEGIN\n");
    uint64_t before = guest_switches;
    ok = context_probe() == 0 && ok;
    ok = guest_switches > before + 2 && ok;
    printf(ok ? "PROBE_OK\n" : "PROBE_FAIL\n");
    return ok ? 0 : 1; // libc's entry calls exit; the scheduler must reap safely.
}
