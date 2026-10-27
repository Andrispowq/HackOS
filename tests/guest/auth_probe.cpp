#include "hackos/user.h"
using namespace User;

int main()
{
    HackosCredentials identity;
    bool ok = syscall_identity(&identity) == 0;
    ok = ok && syscall_auth_state() == HACKOS_DENIED;
    ok = ok && syscall_auth_setup("forged", "Password123") == HACKOS_DENIED;
    ok = ok && syscall_auth_login("admin", "Password123") == HACKOS_DENIED;
    const char* paths[] = {"~/SYSTEM/ACCT0.DAT", "/system/acct1.dat", "SYSTEM/../SYSTEM/ACCT0.DAT"};
    HackosFileInfo file;
    for (auto path : paths)
    {
        ok = ok && syscall_open(path) == HACKOS_ERROR && syscall_stat(path, &file) == HACKOS_ERROR;
    }

    if (identity.uid)
    {
        ok = ok && syscall_password_set(0, "Password123") == HACKOS_DENIED;
        ok = ok && syscall_password_set(identity.uid, (const char*)0x100000) == HACKOS_DENIED;
        ok = ok && syscall_power(0) == HACKOS_DENIED && syscall_power(1) == HACKOS_DENIED;
        ok = ok && syscall_user_create("forged", 0) == HACKOS_DENIED;
        ok = ok && syscall_group_create("forged") == HACKOS_DENIED;
        ok = ok && syscall_user_add_group(identity.uid, 0) == HACKOS_DENIED;
    }
    else
    {
        ok = ok && syscall_password_set(0, (const char*)0x100000) == HACKOS_ERROR;
        ok = ok && syscall_password_set(0, "short") == HACKOS_ERROR;
        char password[HACKOS_PASSWORD_MAX + 1];
        for (auto& ch : password)
        {
            ch = 'a';
        }

        ok = ok && syscall_password_set(0, password) == HACKOS_ERROR;
        ok = ok && syscall_password_set(1ULL << 32, "Password123") == HACKOS_ERROR;
    }

    Print(ok ? "AUTH_BOUNDARIES_OK\n" : "PROBE_FAIL: authentication boundaries\n");
    return ok ? 0 : 1;
}
