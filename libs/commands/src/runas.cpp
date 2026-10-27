#include "accounts.h"
#include "hackos/runtime/path.h"

int main(int argc, char** argv)
{
    if (argc < 3 || !*argv[2])
    {
        Print("Usage: runas <user> <command> [args]\n");
        return 2;
    }

    HackosUserInfo user;
    if (!FindUser(argv[1], user))
    {
        return 1;
    }

    char cwd[256], path[256];
    if (syscall_getcwd(cwd, sizeof(cwd)) != 0)
    {
        return 1;
    }

    bool explicitPath = argv[2][0] == '~';
    for (const char* c = argv[2]; *c; ++c)
    {
        if (*c == '/')
        {
            explicitPath = true;
        }
    }

    if (!NormalizePath(argv[2], explicitPath ? cwd : "~/USR/BIN", path))
    {
        Print("Invalid program path.\n");
        return 1;
    }

    uint64_t pid =
        syscall_spawn_as(path, argc - 2, (const char* const*)(argv + 2), user.credentials.uid, HACKOS_SPAWN_FOREGROUND);
    if (pid == HACKOS_DENIED)
    {
        // A background launcher cannot delegate the terminal. The denied
        // attempt creates no child; retry without requesting terminal input.
        pid = syscall_spawn_as(path, argc - 2, (const char* const*)(argv + 2), user.credentials.uid, 0);
    }
    if (pid == HACKOS_DENIED || pid == HACKOS_ERROR)
    {
        Print(pid == HACKOS_DENIED ? "Permission denied: root required.\n" : "Cannot launch program.\n");
        return 1;
    }

    int64_t status;
    if (syscall_wait(pid, &status, 0) != pid)
    {
        Print("Wait failed.\n");
        return 1;
    }

    return (int)status;
}
