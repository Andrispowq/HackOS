#include "hackos/user.h"

using namespace User;

static bool Check(bool condition, unsigned line)
{
    if (!condition)
    {
        Print("PROBE_FAIL storage line ");
        Number(line);
        Print("\n");
    }

    return condition;
}

#define REQUIRE(condition)                                                                                             \
    if (!Check(condition, __LINE__))                                                                                   \
    return 1

static bool Error(uint64_t result)
{
    return result >= HACKOS_NOT_FOUND;
}

int main(int argc, char** argv)
{
    char bytes[512] = {};
    HackosFileInfo info;
    if (argc == 3 && Equal(argv[1], "foreign"))
    {
        uint64_t handle = 0;
        for (const char* c = argv[2]; *c; ++c)
        {
            handle = handle * 10 + *c - '0';
        }

        REQUIRE(Error(syscall_read(handle, bytes, 1)));
        REQUIRE(Error(syscall_file_write(handle, "x", 1)));
        REQUIRE(Error(syscall_close(handle)));
        REQUIRE(Error(syscall_open("/TMP/OWNED")));
        return 0;
    }

    if (argc == 2 && Equal(argv[1], "guest"))
    {
        REQUIRE(Error(syscall_mkdir("/FORBIDDEN")));
        REQUIRE(Error(syscall_file_save("/USR/HELLO.TXT", "bad", 3, 0)));
        REQUIRE(Error(syscall_unlink("/USR/HELLO.TXT")));
        REQUIRE(Error(syscall_rename("/USR/HELLO.TXT", "/TMP/STOLEN")));
        REQUIRE(Error(syscall_open("/PRIVATE/SECRET")));
        REQUIRE(Error(syscall_readdir("/PRIVATE", 0, &info)));
        REQUIRE(Error(syscall_chdir("/PRIVATE")));
        REQUIRE(Error(syscall_stat("/PRIVATE/SECRET", &info)));
        REQUIRE(Error(syscall_spawn("/USR/BIN/PRINT")));
        REQUIRE(Error(syscall_chmod("/USR/BIN/PRINT", 0755)));
        REQUIRE(Error(syscall_chown("/TMP/OWNED", 0, 0)));
        REQUIRE(Error(syscall_chmod("/SYSTEM/ACCT0.DAT", 0777)));
        uint64_t group = syscall_open("/GROUPFILE");
        REQUIRE(!Error(group));
        REQUIRE(syscall_read(group, bytes, 6) == 6 && Equal(bytes, "shared"));
        REQUIRE(syscall_close(group) == 0);
        REQUIRE(Error(syscall_open_mode("/GROUPFILE", HACKOS_OPEN_WRITE)));
        REQUIRE(syscall_stat("/TMP/OWNED", &info) == 0 && info.uid == 1000 && info.gid == 1000 && info.mode == 0600);
        REQUIRE(syscall_chmod("/TMP/OWNED", 0640) == 0);
        uint64_t handle = syscall_open_mode("/TMP/OWNED", HACKOS_OPEN_READ | HACKOS_OPEN_WRITE);
        REQUIRE(!Error(handle));
        REQUIRE(Error(syscall_open("/TMP/OWNED")));
        REQUIRE(Error(syscall_file_write(handle, (void*)0x100000, 1)));
        REQUIRE(Error(syscall_file_write(handle, bytes, 513)));
        REQUIRE(syscall_seek(handle, 511) == 511);
        REQUIRE(syscall_file_write(handle, "XYZ", 3) == 3);
        REQUIRE(syscall_flush(handle) == 0);
        REQUIRE(syscall_seek(handle, 0) == 0);
        REQUIRE(syscall_read(handle, bytes, sizeof(bytes)) == sizeof(bytes));
        for (unsigned i = 0; i < 511; ++i)
        {
            REQUIRE(bytes[i] == 0);
        }

        REQUIRE(bytes[511] == 'X');
        char number[21];
        uint64_t value = handle;
        unsigned digits = 0;
        do
        {
            number[digits++] = '0' + value % 10;
            value /= 10;
        } while (value);
        for (unsigned i = 0; i < digits / 2; ++i)
        {
            char c = number[i];
            number[i] = number[digits - i - 1];
            number[digits - i - 1] = c;
        }

        number[digits] = 0;
        const char* args[] = {"storageprobe", "foreign", number};
        uint64_t pid = syscall_spawn_args("/USR/BIN/STORPROBE", 3, args);
        REQUIRE(!Error(pid));
        int64_t status = -1;
        REQUIRE(syscall_wait(pid, &status, 0) == pid && status == 0);
        REQUIRE(syscall_close(handle) == 0);
        REQUIRE(Error(syscall_read(handle, bytes, 1)));
        REQUIRE(syscall_rename("/TMP/OWNED", "/TMP/MOVED") == 0);
        REQUIRE(syscall_stat("/TMP/MOVED", &info) == 0 && info.uid == 1000 && info.mode == 0640 && info.size == 514);
        REQUIRE(syscall_file_save("/TMP/NEW", "created", 7, HACKOS_SAVE_CREATE) == 0);
        REQUIRE(syscall_stat("/TMP/NEW", &info) == 0 && info.uid == 1000 && info.mode == 0644);
        Print("STORAGE_GUEST_OK\n");
        return 0;
    }

    if (argc == 2 && Equal(argv[1], "reboot"))
    {
        REQUIRE(syscall_stat("/TMP/MOVED", &info) == 0 && info.uid == 1000 && info.gid == 1000 && info.mode == 0640 &&
                info.size == 514);
        uint64_t handle = syscall_open("/TMP/MOVED");
        REQUIRE(!Error(handle));
        REQUIRE(syscall_seek(handle, 511) == 511);
        REQUIRE(syscall_read(handle, bytes, 3) == 3 && Equal(bytes, "XYZ"));
        REQUIRE(syscall_close(handle) == 0);
        REQUIRE(syscall_stat("/TMP/NEW", &info) == 0 && info.uid == 1000 && info.mode == 0644);
        Print("STORAGE_REBOOT_OK\n");
        return 0;
    }

    REQUIRE(syscall_mkdir("/PRIVATE") == 0);
    REQUIRE(syscall_file_save("/PRIVATE/SECRET", "secret", 6, HACKOS_SAVE_CREATE) == 0);
    REQUIRE(syscall_chmod("/PRIVATE", 0700) == 0);
    REQUIRE(syscall_file_save("/GROUPFILE", "shared", 6, HACKOS_SAVE_CREATE) == 0);
    REQUIRE(syscall_chown("/GROUPFILE", 0, 1000) == 0);
    REQUIRE(syscall_chmod("/GROUPFILE", 0640) == 0);
    REQUIRE(syscall_file_save("/TMP/OWNED", nullptr, 0, HACKOS_SAVE_CREATE) == 0);
    REQUIRE(syscall_chown("/TMP/OWNED", 1000, 1000) == 0);
    REQUIRE(syscall_chmod("/TMP/OWNED", 0600) == 0);
    REQUIRE(syscall_chmod("/USR/BIN/PRINT", 0755) == 0);
    uint64_t executable = syscall_open_mode("/USR/BIN/PRINT", HACKOS_OPEN_WRITE);
    REQUIRE(!Error(executable));
    REQUIRE(Error(syscall_spawn("/USR/BIN/PRINT")));
    REQUIRE(syscall_close(executable) == 0);
    REQUIRE(syscall_chmod("/USR/BIN/PRINT", 0644) == 0);
    REQUIRE(Error(syscall_spawn("/USR/BIN/PRINT")));
    uint64_t read = syscall_open("/GROUPFILE");
    REQUIRE(!Error(read));
    REQUIRE(Error(syscall_file_write(read, "x", 1)));
    REQUIRE(Error(syscall_open_mode("/GROUPFILE", HACKOS_OPEN_WRITE)));
    REQUIRE(syscall_close(read) == 0);
    REQUIRE(Error(syscall_open_mode("/TMP/NO", 0)));
    REQUIRE(Error(syscall_open_mode("/TMP/NO", 8)));
    REQUIRE(Error(syscall_chmod("/TMP/OWNED", 01000)));
    REQUIRE(Error(syscall_chown("/TMP/OWNED", 12345, 0)));
    uint64_t handles[8];
    for (unsigned i = 0; i < 8; ++i)
    {
        handles[i] = syscall_open("/GROUPFILE");
        REQUIRE(!Error(handles[i]));
    }

    REQUIRE(Error(syscall_open_mode("/TMP/NO", HACKOS_OPEN_WRITE | HACKOS_OPEN_CREATE)));
    for (uint64_t handle : handles)
    {
        REQUIRE(syscall_close(handle) == 0);
    }

    REQUIRE(syscall_stat("/TMP/NO", &info) == HACKOS_NOT_FOUND);
    Print("STORAGE_ROOT_OK\n");
    return 0;
}
