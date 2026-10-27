#include "hackos/user.h"
#include "hackos/runtime/secret_input.h"
using namespace User;

int main()
{
    char name[HACKOS_ACCOUNT_NAME] = {};
    char password[HACKOS_PASSWORD_MAX + 1] = {};
    Print("\nHackOS login\n");
    for (;;)
    {
        uint64_t state = syscall_auth_state();
        if (state == HACKOS_AUTH_BROKEN)
        {
            Print("Account storage unavailable or corrupt. Login is disabled; restore the account store offline.\n");
            for (;;)
            {
                syscall_yield();
            }
        }

        if (state != HACKOS_AUTH_SETUP && state != HACKOS_AUTH_LOGIN)
        {
            Print("This program is not the boot login agent.\n");
            return 1;
        }

        if (state == HACKOS_AUTH_SETUP)
        {
            Print("First-boot setup. Create the initial administrator (UID 0).\nUsername: ");
            if (!ReadLine(name, sizeof(name), false) || !NewSecret(password))
            {
                continue;
            }

            Print("Saving account...\n");
            uint64_t result = syscall_auth_setup(name, password);
            EraseSecret(password, sizeof(password));
            if (result == 0)
            {
                Print("Setup complete. Please log in.\n");
            }
            else
            {
                Print(result == HACKOS_NO_ENTROPY ? "Secure randomness unavailable; setup was not saved.\n"
                                                  : "Setup failed. Check the username, password and storage.\n");
            }

            continue;
        }

        Print("Login: ");
        if (!ReadLine(name, sizeof(name), false))
        {
            continue;
        }

        Print("Password: ");
        if (!ReadLine(password, sizeof(password), true))
        {
            continue;
        }

        uint64_t pid;
        do
        {
            pid = syscall_auth_login(name, password);
            if (pid == HACKOS_RETRY)
            {
                syscall_yield();
            }
        } while (pid == HACKOS_RETRY);
        EraseSecret(password, sizeof(password));
        EraseSecret(name, sizeof(name));
        if (pid == HACKOS_ERROR || pid == HACKOS_DENIED)
        {
            Print("Login failed.\n");
            continue;
        }

        syscall_wait(pid, nullptr, 0);
        syscall_yield(); // let idle reclaim session windows/files before prompting
        syscall_console(1);
        Print("Logged out.\n");
    }
}
