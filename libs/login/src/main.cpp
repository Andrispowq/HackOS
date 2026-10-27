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
        if (state != HACKOS_AUTH_BROKEN && state != HACKOS_AUTH_SETUP && state != HACKOS_AUTH_LOGIN)
        {
            Print("This program is not the boot login agent.\n");
            return 1;
        }

        if (state == HACKOS_AUTH_BROKEN)
        {
            Print("Account storage unavailable or corrupt. Login is disabled; restore the account store offline.\n");
        }

        Print("\n1) login\n2) restart\n3) shutdown\nAction: ");
        char action[16] = {};
        if (!ReadLine(action, sizeof(action), false))
        {
            continue;
        }

        for (auto& ch : action)
        {
            if (ch >= 'A' && ch <= 'Z')
            {
                ch += 'a' - 'A';
            }
        }

        if (Equal(action, "2") || Equal(action, "restart") || Equal(action, "3") || Equal(action, "shutdown"))
        {
            uint64_t operation = Equal(action, "2") || Equal(action, "restart") ? 1 : 0;
            syscall_power(operation);
            Print("Power request failed.\n");
            continue;
        }

        if (!Equal(action, "1") && !Equal(action, "login"))
        {
            Print("Choose 1-3 or login, restart, shutdown.\n");
            continue;
        }

        if (state == HACKOS_AUTH_BROKEN)
        {
            continue;
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
