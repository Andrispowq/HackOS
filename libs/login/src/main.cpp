#include "hackos/user.h"
#include "hackos/gui.h"
#include "hackos/runtime/secret_input.h"
using namespace User;

static int RecoveryLogin()
{
    char name[HACKOS_ACCOUNT_NAME] = {};
    char password[HACKOS_PASSWORD_MAX + 1] = {};
    syscall_console(1);
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

        Print("\n1) login\n2) restart\n3) shutdown\nconsole) recovery shell\nAction: ");
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

        if (!Equal(action, "1") && !Equal(action, "login") && !Equal(action, "console"))
        {
            Print("Choose 1-3 or login, restart, shutdown.\n");
            continue;
        }

        if (state == HACKOS_AUTH_BROKEN)
        {
            continue;
        }

        syscall_console(1);
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
            pid = Equal(action, "console") ? syscall_auth_login(name, password) : syscall_auth_desktop(name, password);
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

static Gui::Window loginWindow;
static Gui::View loginView;
static unsigned usernameField, passwordField, confirmField, submitButton;
static unsigned shutdownButton, restartButton, recoveryButton;

static void ClearSecrets()
{
    loginView.SetText(passwordField, "");
    loginView.SetText(confirmField, "");
}

static void Message(const char* title, const char* text)
{
    const char* buttons[] = {"OK"};
    loginView.OpenDialog(title, text, buttons, 1, 0);
}

static bool OpenLogin(uint64_t state)
{
    syscall_console(1);
    HackosWindowSpec spec = {1, 1, 640, 480, "HackOS sign in"};
    if (!loginWindow.Open(spec))
    {
        return false;
    }
    syscall_window_mode(loginWindow.Handle(), HACKOS_WINDOW_MAXIMIZE);
    loginView.Init();
    auto theme = Gui::DefaultTheme();
    theme.background = theme.panel = 0xff152738;
    loginView.SetTheme(theme);
    loginView.SetLayout(0, 0, 0, 0, 0, 0);
    auto top = loginView.Add(Gui::Kind::Column, 0);
    loginView.SetLayout(top, 0, 0, 1, 0, 0);
    auto middle = loginView.Add(Gui::Kind::Row, 0);
    loginView.SetLayout(middle, 0, 480, 0, 0, 0);
    auto left = loginView.Add(Gui::Kind::Column, middle);
    loginView.SetLayout(left, 0, 0, 1, 0, 0);
    auto form = loginView.Add(Gui::Kind::Column, middle);
    loginView.SetLayout(form, 440, 480, 0, 20, 8);
    auto right = loginView.Add(Gui::Kind::Column, middle);
    loginView.SetLayout(right, 0, 0, 1, 0, 0);
    auto bottom = loginView.Add(Gui::Kind::Column, 0);
    loginView.SetLayout(bottom, 0, 0, 1, 0, 0);
    loginView.Add(Gui::Kind::Label, form, "Welcome to HackOS");
    loginView.Add(Gui::Kind::Label, form,
                  state == HACKOS_AUTH_SETUP ? "Create your administrator account" : "Sign in to your desktop");
    loginView.Add(Gui::Kind::Label, form, "Username");
    usernameField = loginView.Add(Gui::Kind::TextField, form);
    loginView.Add(Gui::Kind::Label, form, "Password");
    passwordField = loginView.Add(Gui::Kind::TextField, form);
    loginView.SetSecret(passwordField, true);
    auto confirmLabel = loginView.Add(Gui::Kind::Label, form, "Confirm password (8-128 characters)");
    confirmField = loginView.Add(Gui::Kind::TextField, form);
    loginView.SetSecret(confirmField, true);
    loginView.SetVisible(confirmLabel, state == HACKOS_AUTH_SETUP);
    loginView.SetVisible(confirmField, state == HACKOS_AUTH_SETUP);
    submitButton = loginView.Add(Gui::Kind::Button, form, state == HACKOS_AUTH_SETUP ? "Create account" : "Log in");
    auto power = loginView.Add(Gui::Kind::Row, form);
    loginView.SetLayout(power, 0, 32, 0, 0, 8);
    shutdownButton = loginView.Add(Gui::Kind::Button, power, "Shut down");
    restartButton = loginView.Add(Gui::Kind::Button, power, "Restart");
    loginView.SetLayout(shutdownButton, 0, 32, 1);
    loginView.SetLayout(restartButton, 0, 32, 1);
    recoveryButton = loginView.Add(Gui::Kind::Button, form, "Recovery console (Esc)");
    loginView.SetFocus(usernameField);
    if (state == HACKOS_AUTH_BROKEN)
    {
        loginView.SetEnabled(submitButton, false);
        loginView.SetEnabled(usernameField, false);
        loginView.SetEnabled(passwordField, false);
        Message("Login unavailable", "Account storage is unavailable or corrupt.\nRestore the account store offline.");
    }
    loginWindow.Paint(loginView);
    Print("LOGIN_GUI_READY\n");
    return true;
}

int main()
{
    auto state = syscall_auth_state();
    if (state != HACKOS_AUTH_BROKEN && state != HACKOS_AUTH_SETUP && state != HACKOS_AUTH_LOGIN)
    {
        Print("This program is not the boot login agent.\n");
        return 1;
    }
    for (;;)
    {
        state = syscall_auth_state();
        if (!OpenLogin(state))
        {
            return RecoveryLogin();
        }
        bool rebuild = false;
        while (!rebuild)
        {
            HackosWindowEvent2 event = {};
            for (unsigned n = 0; n < 32 && syscall_window_event2(loginWindow.Handle(), &event) == 1; ++n)
            {
                bool modal = loginView.DialogOpen();
                bool down = event.event.type == HACKOS_WINDOW_KEY_DOWN && !(event.flags & HACKOS_KEY_REPEAT);
                if (event.event.type == HACKOS_WINDOW_CLOSE_REQUEST)
                {
                    loginWindow.ReplyClose(false);
                    continue;
                }
                if (event.event.type == HACKOS_WINDOW_OVERFLOW || event.event.type == HACKOS_WINDOW_INPUT_RESET)
                {
                    HackosWindowInputState input = {};
                    syscall_window_input(loginWindow.Handle(), HACKOS_INPUT_QUERY, &input);
                    ClearSecrets();
                    loginView.ResetInput(input.focused);
                    continue;
                }
                auto action = loginView.Handle(event);
                if (!modal && down && event.event.key == HACKOS_PHYSICAL_ENTER &&
                    (loginView.Focus() == usernameField || loginView.Focus() == passwordField ||
                     loginView.Focus() == confirmField))
                {
                    action = {Gui::ActionKind::Activate, submitButton, 0};
                }
                if ((!modal && down && event.event.key == 1) ||
                    (action.kind == Gui::ActionKind::Activate && action.widget == recoveryButton))
                {
                    ClearSecrets();
                    loginWindow.Close();
                    return RecoveryLogin();
                }
                if (action.kind != Gui::ActionKind::Activate)
                {
                    continue;
                }
                if (action.widget == shutdownButton || action.widget == restartButton)
                {
                    ClearSecrets();
                    syscall_power(action.widget == restartButton ? 1 : 0);
                    Message("Power request failed", "The machine could not complete this request.");
                }
                else if (action.widget == submitButton && state != HACKOS_AUTH_BROKEN)
                {
                    auto name = loginView.Get(usernameField);
                    auto password = loginView.Get(passwordField);
                    auto confirm = loginView.Get(confirmField);
                    if (!name->length || name->length >= HACKOS_ACCOUNT_NAME || !password->length ||
                        password->length > HACKOS_PASSWORD_MAX ||
                        (state == HACKOS_AUTH_SETUP && (password->length < 8 || !Equal(password->text, confirm->text))))
                    {
                        ClearSecrets();
                        Message("Check your details",
                                state == HACKOS_AUTH_SETUP
                                    ? "Use a valid username and matching\npasswords of 8-128 characters."
                                    : "Enter your username and password.");
                        continue;
                    }
                    uint64_t result;
                    do
                    {
                        result = state == HACKOS_AUTH_SETUP ? syscall_auth_setup(name->text, password->text)
                                                            : syscall_auth_desktop(name->text, password->text);
                        if (result == HACKOS_RETRY)
                        {
                            syscall_sleep(100);
                        }
                    } while (result == HACKOS_RETRY);
                    ClearSecrets();
                    if (state == HACKOS_AUTH_SETUP && result == 0)
                    {
                        Print("GUI_SETUP_OK\n");
                        rebuild = true;
                    }
                    else if (state != HACKOS_AUTH_SETUP && result != HACKOS_ERROR && result != HACKOS_DENIED)
                    {
                        loginView.SetText(usernameField, "");
                        loginWindow.Close();
                        Print("GUI_LOGIN_OK\n");
                        syscall_wait(result, nullptr, 0);
                        syscall_yield();
                        rebuild = true;
                    }
                    else
                    {
                        Print("GUI_LOGIN_FAILED\n");
                        Message(state == HACKOS_AUTH_SETUP ? "Setup failed" : "Login failed",
                                state == HACKOS_AUTH_SETUP
                                    ? "Could not save the account. Check the\nusername, secure randomness and storage."
                                    : "The username or password is incorrect.\nPlease try again.");
                        loginView.SetFocus(passwordField);
                    }
                    if (rebuild)
                    {
                        break;
                    }
                }
            }
            if (!rebuild)
            {
                if (loginView.Dirty())
                {
                    HackosWindowState geometry = {};
                    if (!syscall_window_state(loginWindow.Handle(), &geometry))
                    {
                        // The form is 440x480; centered dialogs extend to 520x184.
                        // Initial paint and geometry changes still redraw the full client.
                        unsigned width = geometry.width < 520 ? geometry.width : 520;
                        unsigned height = geometry.height < 480 ? geometry.height : 480;
                        Gui::Rect damage = {(int)(geometry.width - width) / 2, (int)(geometry.height - height) / 2,
                                            (int)width, (int)height};
                        loginWindow.Paint(loginView, &damage);
                    }
                }
                loginWindow.Wait(100);
            }
        }
        ClearSecrets();
        loginWindow.Close();
    }
}
