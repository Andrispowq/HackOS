#include "hackos/user.h"

using namespace User;
static char data[HACKOS_FILE_LIMIT + 1];

static bool Contents(const char* path, const char* expected)
{
    uint64_t handle = syscall_open(path);
    if (handle == HACKOS_ERROR)
    {
        return false;
    }
    char buffer[64] = {};
    uint64_t length = syscall_read(handle, buffer, sizeof(buffer));
    syscall_close(handle);
    return length == Length(expected) && Equal(buffer, expected);
}

int main(int argc, char**)
{
    if (argc > 1)
    {
        uint64_t held = syscall_open("/FILES/HELD.TXT");
        if (held == HACKOS_ERROR)
        {
            return 1;
        }
        Print("FILE_READER_READY\n");
        for (;;)
        {
            syscall_yield();
        }
    }

    bool ok = syscall_mkdir("/FILES") == 0;
    ok = syscall_mkdir("/FILES") == HACKOS_ERROR && ok;
    ok = syscall_mkdir((const char*)0x100000) == HACKOS_ERROR && ok;
    ok = syscall_mkdir("") == HACKOS_ERROR && ok;
    ok = syscall_file_save("/FILES/A.TXT", "original", 8, HACKOS_SAVE_CREATE) == 0 && ok;
    ok = syscall_file_save("/FILES/A.TXT", "bad", 3, HACKOS_SAVE_CREATE) == HACKOS_ERROR && ok;
    ok = syscall_file_save("/FILES/A.TXT", (void*)0x100000, 1, 0) == HACKOS_ERROR && ok;
    ok = syscall_file_save("/FILES/A.TXT", data, HACKOS_FILE_LIMIT + 1, 0) == HACKOS_ERROR && ok;
    ok = syscall_file_save("/FILES/A.TXT", data, ~0ULL, 0) == HACKOS_ERROR && ok;
    ok = syscall_file_save("/FILES/A.TXT", data, 1, 2) == HACKOS_ERROR && ok;
    ok = Contents("/FILES/A.TXT", "original") && ok;
    uint64_t handle = syscall_open("/FILES/A.TXT");
    ok = handle != HACKOS_ERROR && ok;
    ok = syscall_unlink("/FILES/./a.txt") == HACKOS_ERROR && ok;
    ok = syscall_rename("/FILES/A.TXT", "/FILES/B.TXT") == HACKOS_ERROR && ok;
    ok = syscall_file_save("/FILES/A.TXT", "bad", 3, 0) == HACKOS_ERROR && ok;
    syscall_close(handle);
    ok = syscall_read(handle, data, 1) == HACKOS_ERROR && ok;
    ok = syscall_file_save("/FILES/B.TXT", "target", 6, HACKOS_SAVE_CREATE) == 0 && ok;
    ok = syscall_rename("/FILES/A.TXT", "/FILES/B.TXT") == HACKOS_ERROR && ok;
    ok = syscall_rename("/FILES/A.TXT", (const char*)0x100000) == HACKOS_ERROR && ok;
    ok = Contents("/FILES/A.TXT", "original") && Contents("/FILES/B.TXT", "target") && ok;
    ok = syscall_unlink("/FILES") == HACKOS_ERROR && ok;
    ok = syscall_rename("/FILES", "/OTHER") == HACKOS_ERROR && ok;
    ok = syscall_file_save("/FILES", "bad", 3, 0) == HACKOS_ERROR && ok;
    ok = syscall_rename("/FILES/A.TXT", "fs1:/FILE") == HACKOS_ERROR && ok;
    const char* protectedPaths[] = {"/SYSTEM/ACCT0.DAT", "/system/../SYSTEM/ACCT1.DAT"};
    for (const char* path : protectedPaths)
    {
        ok = syscall_unlink(path) == HACKOS_ERROR && ok;
        ok = syscall_rename(path, "/FILES/STOLEN") == HACKOS_ERROR && ok;
        ok = syscall_rename("/FILES/A.TXT", path) == HACKOS_ERROR && ok;
        ok = syscall_file_save(path, "bad", 3, 0) == HACKOS_ERROR && ok;
    }

    for (unsigned i = 0; i < HACKOS_FILE_LIMIT; ++i)
    {
        data[i] = 'a' + i % 26;
    }

    ok = syscall_file_save("/FILES/LIMIT.TXT", data, HACKOS_FILE_LIMIT, HACKOS_SAVE_CREATE) == 0 && ok;
    HackosFileInfo info;
    ok = syscall_stat("/FILES/LIMIT.TXT", &info) == 0 && info.size == HACKOS_FILE_LIMIT && ok;
    ok = syscall_file_save("/FILES/HELD.TXT", "held", 4, HACKOS_SAVE_CREATE) == 0 && ok;
    const char* args[] = {"fileprobe", "hold"};
    uint64_t child = syscall_spawn_args("/USR/BIN/FILEPROBE", 2, args);
    ok = child != HACKOS_ERROR && ok;
    // Parent leaves the child running so the shell suite can test another
    // process's live handle and kill/reclamation before trying again.
    Print(ok ? "FILE_ABI_OK\n" : "PROBE_FAIL file ABI\n");
    return ok ? 0 : 1;
}
