#include "hackos/gui.h"
#include "hackos/stdio.h"
#include "hackos/user.h"
using namespace User;
static Gui::Window panel, background;
static Gui::View view, icons;
static unsigned buttons[15], statusLabel;
static uint64_t handles[15], children[16];
static HackosDesktopEntry entries[15];

static void Launch(const char* path, bool widgets = false)
{
    for (auto& child : children)
    {
        if (child)
        {
            continue;
        }
        const char* args[] = {path, "--widgets"};
        auto pid = syscall_spawn_args(path, widgets ? 2 : 1, args);
        if (pid != HACKOS_ERROR)
        {
            child = pid;
            view.SetText(statusLabel, "Application launched.");
        }
        if (pid == HACKOS_ERROR)
        {
            view.SetText(statusLabel, "Launch failed.");
        }
        return;
    }
    view.SetText(statusLabel, "Too many launched applications.");
}

int main()
{
    HackosDesktopEntry screen = {};
    if (syscall_desktop(HACKOS_DESKTOP_SCREEN, 0, &screen) != 1)
    {
        Print("Desktop requires authenticated session authority.\n");
        return 1;
    }
    HackosWindowSpec spec = {1, screen.height - 132, screen.width - 2, 100, "HackOS desktop"};
    if (!panel.Open(spec))
    {
        return 1;
    }
    if (syscall_desktop(HACKOS_DESKTOP_PANEL, panel.Handle(), nullptr) != 0)
    {
        return 1;
    }
    view.Init();
    view.SetLayout(0, 0, 0, 0, 6, 4);
    auto launcher = view.Add(Gui::Kind::Row, 0);
    view.SetLayout(launcher, 0, 36, 0, 0, 6);
    auto terminal = view.Add(Gui::Kind::Button, launcher, "New terminal");
    auto widgets = view.Add(Gui::Kind::Button, launcher, "Widgets");
    auto minimize = view.Add(Gui::Kind::Button, launcher, "Minimize selected");
    auto close = view.Add(Gui::Kind::Button, launcher, "Close selected");
    auto logout = view.Add(Gui::Kind::Button, launcher, "Log out");
    auto files = view.Add(Gui::Kind::Button, launcher, "Files");
    auto editor = view.Add(Gui::Kind::Button, launcher, "Editor");
    auto settings = view.Add(Gui::Kind::Button, launcher, "Settings");
    statusLabel = view.Add(Gui::Kind::Label, launcher, "Ready");
    view.SetLayout(statusLabel, 0, 32, 1, 0, 0);
    auto clockLabel = view.Add(Gui::Kind::Label, launcher, "--:--:-- UTC");
    view.SetLayout(clockLabel, 104, 32);
    unsigned lastSecond = 60;
    auto tasks = view.Add(Gui::Kind::Row, 0);
    view.SetLayout(tasks, 0, 36, 0, 0, 3);
    for (unsigned i = 0; i < 15; ++i)
    {
        buttons[i] = view.Add(Gui::Kind::Button, tasks, "");
        view.SetLayout(buttons[i], 0, 32, 1, 0, 0);
        view.SetVisible(buttons[i], false);
    }
    icons.Init();
    auto palette = Gui::DefaultTheme();
    palette.background = palette.panel = 0xff172b3b;
    icons.SetTheme(palette);
    auto terminalIcon = icons.Add(Gui::Kind::IconButton, 0, "Terminal");
    auto filesIcon = icons.Add(Gui::Kind::IconButton, 0, "Files");
    icons.SetLayout(terminalIcon, 112, 96);
    icons.SetLayout(filesIcon, 112, 96);
    icons.SetIcon(terminalIcon, 1);
    icons.SetIcon(filesIcon, 2);
    HackosWindowSpec backgroundSpec = {1, 1, screen.width - 2, screen.height - 164, "Desktop icons"};
    if (!background.Open(backgroundSpec) || syscall_desktop(HACKOS_DESKTOP_BACKGROUND, background.Handle(), nullptr))
    {
        background.Close();
        panel.Close();
        return 1;
    }
    uint64_t selected = 0;
    Launch("~/USR/BIN/TERM");
    Print("DESKTOP_READY\n");
    for (;;)
    {
        HackosClock now = {};
        if (syscall_clock(&now) == 0 && now.second != lastSecond)
        {
            char text[] = "00:00:00 UTC";
            text[0] += now.hour / 10;
            text[1] += now.hour % 10;
            text[3] += now.minute / 10;
            text[4] += now.minute % 10;
            text[6] += now.second / 10;
            text[7] += now.second % 10;
            view.SetText(clockLabel, text);
            lastSecond = now.second;
        }
        for (auto& child : children)
        {
            int64_t status = 0;
            if (child && syscall_wait(child, &status, HACKOS_WAIT_NOHANG) == child)
            {
                child = 0;
                view.SetText(statusLabel, status == 1 ? "Launch failed (resource limit)." : "Ready");
            }
        }
        for (unsigned i = 0; i < 15; ++i)
        {
            HackosDesktopEntry entry = {};
            auto found = syscall_desktop(HACKOS_DESKTOP_ENTRY, i, &entry) == 1;
            if (!found)
            {
                entry = {};
            }
            if (entry.handle != entries[i].handle || entry.mode != entries[i].mode ||
                entry.focused != entries[i].focused)
            {
                view.SetVisible(buttons[i], found);
                char title[48];
                snprintf(title, sizeof(title), "%s%s",
                         entry.mode & HACKOS_WINDOW_MINIMIZED ? "_ "
                         : entry.focused                      ? "> "
                                                              : "",
                         entry.title);
                view.SetText(buttons[i], title);
                entries[i] = entry;
            }
            handles[i] = entry.handle;
        }
        Gui::Action action;
        for (unsigned n = 0; n < 32 && panel.Next(view, action) == 1; ++n)
        {
            if (action.kind == Gui::ActionKind::Close)
            {
                panel.ReplyClose(false);
            }
            if (action.kind == Gui::ActionKind::Dialog && action.value == 0)
            {
                syscall_session_logout();
            }
            if (action.kind != Gui::ActionKind::Activate)
            {
                continue;
            }
            if (action.widget == terminal)
            {
                Launch("~/USR/BIN/TERM");
            }
            else if (action.widget == files)
            {
                Launch("~/USR/BIN/FILES");
            }
            else if (action.widget == settings)
            {
                Launch("~/USR/BIN/SETTINGS");
            }
            else if (action.widget == editor)
            {
                Launch("~/USR/BIN/EDITOR");
            }
            else if (action.widget == widgets)
            {
                Launch("~/USR/BIN/WINDOW", true);
            }
            else if (action.widget == logout)
            {
                const char* labels[] = {"Log out", "Cancel"};
                view.OpenDialog("End session?", "All terminals, jobs and windows will close.", labels, 2, 1);
            }
            else if (action.widget == minimize && selected)
            {
                syscall_desktop(HACKOS_DESKTOP_MINIMIZE, selected, nullptr);
            }
            else if (action.widget == close && selected)
            {
                syscall_desktop(HACKOS_DESKTOP_CLOSE, selected, nullptr);
            }
            else
            {
                for (unsigned i = 0; i < 15; ++i)
                {
                    if (action.widget == buttons[i] && handles[i])
                    {
                        selected = handles[i];
                        syscall_desktop(HACKOS_DESKTOP_FOCUS, selected, nullptr);
                    }
                }
            }
        }
        for (unsigned n = 0; n < 16 && background.Next(icons, action) == 1; ++n)
        {
            if (action.kind == Gui::ActionKind::Close)
            {
                background.ReplyClose(false);
            }
            else if (action.kind == Gui::ActionKind::Activate)
            {
                if (action.widget == terminalIcon)
                {
                    Launch("~/USR/BIN/TERM");
                }
                else if (action.widget == filesIcon)
                {
                    Launch("~/USR/BIN/FILES");
                }
            }
        }
        if (icons.Dirty())
        {
            background.Paint(icons);
        }
        if (view.Dirty())
        {
            panel.Paint(view);
        }
        HackosPollFd ready[] = {{panel.Handle(), HACKOS_POLL_WINDOW | HACKOS_POLL_READ, 0},
                                {background.Handle(), HACKOS_POLL_WINDOW | HACKOS_POLL_READ, 0}};
        syscall_poll(ready, 2, 100);
    }
}
