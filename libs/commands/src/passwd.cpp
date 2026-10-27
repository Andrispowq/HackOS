#include "accounts.h"
#include "hackos/runtime/secret_input.h"

int main(int argc, char** argv)
{
    if (argc < 1 || argc > 2)
    {
        Print("Usage: passwd [user]\n");
        return 2;
    }

    HackosCredentials identity;
    if (syscall_identity(&identity) != 0 || identity.uid != 0)
    {
        Print("Permission denied: root required.\n");
        return 1;
    }

    HackosUserInfo user;
    uint64_t uid = identity.uid;
    if (argc == 2)
    {
        if (!FindUser(argv[1], user))
        {
            return 1;
        }

        uid = user.credentials.uid;
    }

    char password[HACKOS_PASSWORD_MAX + 1] = {};
    if (!NewSecret(password))
    {
        return 1;
    }

    Print("Saving password...\n");
    uint64_t result = syscall_password_set(uid, password);
    EraseSecret(password, sizeof(password));
    if (result == HACKOS_NO_ENTROPY)
    {
        Print("Secure randomness unavailable; password was not saved.\n");
        return 1;
    }

    return AccountResult(result, "Password saved.");
}
