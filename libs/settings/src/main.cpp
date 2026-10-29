#include "hackos/gui.h"
#include "hackos/stdio.h"
#include "hackos/user.h"

using namespace User;
static Gui::Window window;
static Gui::View view;
static HackosCredentials identity;
static HackosUserInfo users[32];
static HackosGroupInfo groups[32];
static unsigned userCount, groupCount, selectedUser, selectedGroup, currentTab;
static unsigned tabs[9], status, userList, groupList, userName, groupName, details;
static unsigned createUser, renameUser, createGroup, renameGroup, addGroup, removeGroup, primaryGroup;
static unsigned oldPassword, password, confirmation, changePassword, resetPassword;
static unsigned terminalFields[3], terminalApply[3], terminalReset;
static char rows[512][160], userRows[32][80], groupRows[32][80];
static const char *rowItems[512], *userItems[32], *groupItems[32];
static unsigned rowCount;
static const char* titles[] = {"System",   "CPU",      "Memory",   "Devices", "Display",
                               "Firmware", "Accounts", "Password", "Terminal"};

static unsigned Row()
{
    auto row = view.Add(Gui::Kind::Row, 0);
    view.SetLayout(row, 0, 32, 0, 0, 6);
    return row;
}

static unsigned Field(unsigned row, const char* label, bool secret = false)
{
    auto caption = view.Add(Gui::Kind::Label, row, label);
    view.SetLayout(caption, 180, 32);
    auto field = view.Add(Gui::Kind::TextField, row);
    view.SetLayout(field, 0, 32, 1);
    view.SetSecret(field, secret);
    return field;
}

static void Message(uint64_t result)
{
    const char* text = result == HACKOS_DENIED       ? "Permission denied (or incorrect current password)."
                       : result == HACKOS_RETRY      ? "Please wait one second before retrying."
                       : result == HACKOS_NO_ENTROPY ? "Secure randomness unavailable. Password unchanged."
                       : result >= HACKOS_NOT_FOUND
                           ? "Change failed. Check names, groups, limits and disk availability."
                           : "Saved. Group changes apply on next login; IDs and file ownership are preserved.";
    view.SetText(status, text);
}

static void Accounts()
{
    userCount = groupCount = 0;
    while (userCount < 32 && syscall_user_info(userCount, &users[userCount]) == 1)
    {
        snprintf(userRows[userCount], sizeof(userRows[0]), "%-31s  UID %5u%s", users[userCount].name,
                 users[userCount].credentials.uid, users[userCount].credentials.uid == 0 ? " (administrator)" : "");
        userItems[userCount] = userRows[userCount];
        ++userCount;
    }
    while (groupCount < 32 && syscall_group_info(groupCount, &groups[groupCount]) == 1)
    {
        snprintf(groupRows[groupCount], sizeof(groupRows[0]), "%-31s  GID %5u", groups[groupCount].name,
                 groups[groupCount].gid);
        groupItems[groupCount] = groupRows[groupCount];
        ++groupCount;
    }
    if (selectedUser >= userCount)
    {
        selectedUser = 0;
    }
    if (selectedGroup >= groupCount)
    {
        selectedGroup = 0;
    }
}

static void AccountSelection()
{
    if (!userCount || !groupCount)
    {
        return;
    }
    view.SetText(userName, users[selectedUser].name);
    view.SetText(groupName, groups[selectedGroup].name);
    auto& c = users[selectedUser].credentials;
    char text[256];
    int length = snprintf(text, sizeof(text), "UID %u | Primary GID %u | Supplementary GIDs:", c.uid, c.gid);
    for (unsigned i = 0; i < c.groupCount && length > 0 && length < 240; ++i)
    {
        length += snprintf(text + length, sizeof(text) - length, " %u", c.groups[i]);
    }
    view.SetText(details, text);
}

// Every row owns its storage; all firmware/kernel strings are copied records.
template <typename... Args> static void Line(const char* format, Args... args)
{
    if (rowCount == 512)
    {
        return;
    }
    snprintf(rows[rowCount], sizeof(rows[0]), format, args...);
    rowItems[rowCount] = rows[rowCount];
    ++rowCount;
}

static void Hardware()
{
    rowCount = 0;
    HackosHardwareInfo h = {};
    if (syscall_hardware(HACKOS_HW_SYSTEM, 0, &h) != 1)
    {
        Line("Hardware information requires an administrator account.");
        Line("Account and group information remains available in Accounts.");
    }
    else if (currentTab == 0)
    {
        Line("HackOS | x86-64 | %s boot", h.system.uefi ? "UEFI" : "BIOS");
        Line("Firmware vendor: %s | ACPI revision: %u", h.system.oem, h.system.acpiRevision);
        Line("Timer: %u Hz | PCI records: %u | Dropped: %u", h.system.timerHz, h.system.pciStored, h.system.pciDropped);
        Line("Signed in with UID %u and primary GID %u", identity.uid, identity.gid);
        Line("Experimental operating system. Hardware discovery does not imply driver support.");
    }
    else if (currentTab == 1 && syscall_hardware(HACKOS_HW_CPU, 0, &h) == 1)
    {
        Line("%s", h.cpu.brand);
        Line("Vendor: %s", h.cpu.vendor);
        Line("Family: %u | Model: %u | Stepping: %u", h.cpu.family, h.cpu.model, h.cpu.stepping);
        Line("Processors discovered: %u | Active: %u", h.cpu.discoveredCpus, h.cpu.activeCpus);
        Line("Features EDX: %08x | ECX: %08x | Extended EDX: %08x", h.cpu.featuresEdx, h.cpu.featuresEcx,
             h.cpu.extendedEdx);
        Line("Only one processor executes kernel tasks; SMP is not enabled.");
    }
    else if (currentTab == 2)
    {
        if (syscall_hardware(HACKOS_HW_MEMORY, 0, &h) == 1)
        {
            Line("Free: %lu KiB | Used: %lu KiB | Reserved: %lu KiB", h.memory.freeBytes / 1024,
                 h.memory.usedBytes / 1024, h.memory.reservedBytes / 1024);
            Line("Firmware usable: %lu KiB | Page size: %u bytes", h.memory.firmwareUsableBytes / 1024,
                 h.memory.pageSize);
            Line("%-16s  %20s  %4s  %s", "PHYSICAL ADDRESS", "BYTES", "TYPE", "USABLE");
        }
        for (unsigned i = 0; i < 255 && syscall_hardware(HACKOS_HW_MEMORY_MAP, i, &h) == 1; ++i)
        {
            Line("%016lx  %20lu  %4u  %s", h.region.address, h.region.size, h.region.type,
                 h.region.usable ? "yes" : "no");
        }
    }
    else if (currentTab == 3)
    {
        Line("%-6s  %-10s  %12s  %12s  %s", "DISK", "CONTROLLER", "MiB", "SECTOR BYTES", "BOOT");
        for (unsigned i = 0; i < 32 && syscall_hardware(HACKOS_HW_DISK, i, &h) == 1; ++i)
        {
            Line("%6u  %-10s  %12lu  %12u  %s", h.disk.slot, h.disk.type == 1 ? "ATA" : "AHCI",
                 h.disk.sectors * h.disk.sectorSize / 1048576, h.disk.sectorSize, h.disk.bootDevice ? "yes" : "no");
        }
        Line("");
        Line("%-12s  %-11s  %-11s  %s", "PCI ADDRESS", "VENDOR:DEV", "CLASS/SUB/IF", "DESCRIPTION");
        for (unsigned i = 0; i < 512 && syscall_hardware(HACKOS_HW_PCI, i, &h) == 1; ++i)
        {
            Line("%04x:%02x:%02x.%u  %04x:%04x    %02x/%02x/%02x     %s", h.pci.segment, h.pci.bus, h.pci.slot,
                 h.pci.function, h.pci.vendor, h.pci.device, h.pci.classCode, h.pci.subclass, h.pci.interface,
                 h.pci.classCode == 12 && h.pci.subclass == 3 ? "USB controller (discovery only)" : "");
        }
    }
    else if (currentTab == 4)
    {
        if (syscall_hardware(HACKOS_HW_DISPLAY, 0, &h) == 1)
        {
            Line("Framebuffer: %u x %u | %u bits per pixel", h.display.width, h.display.height, h.display.bitsPerPixel);
            Line("Pitch: %u bytes | Software rendering; no GPU acceleration", h.display.pitch);
        }
        if (syscall_hardware(HACKOS_HW_PS2, 0, &h) == 1)
        {
            Line("PS/2 keyboard: %s | Mouse: %s | Mouse ID: %u",
                 h.ps2.keyboardRegistered ? "registered" : "unavailable",
                 h.ps2.mouseEnabled ? "enabled" : "unavailable", h.ps2.mouseId);
        }
    }
    else if (currentTab == 5)
    {
        Line("%-8s  %8s  %10s  %-8s  %s", "TABLE", "REVISION", "BYTES", "OEM", "TABLE ID");
        for (unsigned i = 0; i < 255 && syscall_hardware(HACKOS_HW_ACPI, i, &h) == 1; ++i)
        {
            Line("%-8s  %8u  %10u  %-8s  %s", h.acpi.signature, h.acpi.revision, h.acpi.length, h.acpi.oem,
                 h.acpi.tableId);
        }
    }
    auto list = view.Add(Gui::Kind::List, 0);
    view.SetLayout(list, 0, 0, 1);
    view.SetItems(list, rowItems, rowCount);
}

static void ClearView()
{
    volatile unsigned char* bytes = (volatile unsigned char*)&view;
    for (unsigned i = 0; i < sizeof(view); ++i)
    {
        bytes[i] = 0;
    }
}

static void Build()
{
    // Erase old fields, including secret backing storage, when changing pages.
    ClearView();
    view.Init();
    auto navigation = Row();
    for (unsigned i = 0; i < 9; ++i)
    {
        tabs[i] = view.Add(Gui::Kind::Button, navigation, titles[i]);
        view.SetEnabled(tabs[i], currentTab != i);
    }
    view.Add(Gui::Kind::Label, 0, titles[currentTab]);
    if (currentTab < 6)
    {
        Hardware();
    }
    else if (currentTab == 6)
    {
        Accounts();
        view.Add(Gui::Kind::Label, 0,
                 identity.uid == 0 ? "Select a user and a group. New users are locked until you set their password."
                                   : "Read-only account directory. Only an administrator may edit accounts or groups.");
        auto lists = view.Add(Gui::Kind::Row, 0);
        view.SetLayout(lists, 0, 0, 1, 0, 8);
        userList = view.Add(Gui::Kind::List, lists);
        groupList = view.Add(Gui::Kind::List, lists);
        view.SetLayout(userList, 0, 0, 1);
        view.SetLayout(groupList, 0, 0, 1);
        view.SetItems(userList, userItems, userCount);
        view.SetItems(groupList, groupItems, groupCount);
        // SetItems starts at the first entry; use matching selection state.
        selectedUser = selectedGroup = 0;
        details = view.Add(Gui::Kind::Label, 0);
        auto row = Row();
        userName = Field(row, "User name");
        createUser = view.Add(Gui::Kind::Button, row, "Create user");
        renameUser = view.Add(Gui::Kind::Button, row, "Rename user");
        row = Row();
        groupName = Field(row, "Group name");
        createGroup = view.Add(Gui::Kind::Button, row, "Create group");
        renameGroup = view.Add(Gui::Kind::Button, row, "Rename group");
        row = Row();
        addGroup = view.Add(Gui::Kind::Button, row, "Add membership");
        removeGroup = view.Add(Gui::Kind::Button, row, "Remove membership");
        primaryGroup = view.Add(Gui::Kind::Button, row, "Set primary group");
        unsigned controls[] = {userName,    groupName, createUser,  renameUser,  createGroup,
                               renameGroup, addGroup,  removeGroup, primaryGroup};
        for (auto id : controls)
        {
            view.SetEnabled(id, identity.uid == 0);
        }
        AccountSelection();
    }
    else if (currentTab == 7)
    {
        Accounts();
        char text[160];
        snprintf(text, sizeof(text), "Change your own password (UID %u). Use 8-128 printable ASCII characters.",
                 identity.uid);
        view.Add(Gui::Kind::Label, 0, text);
        oldPassword = Field(Row(), "Current password", true);
        password = Field(Row(), "New password", true);
        confirmation = Field(Row(), "Confirm password", true);
        changePassword = view.Add(Gui::Kind::Button, 0, "Change my password");
        if (identity.uid == 0)
        {
            snprintf(text, sizeof(text), "Administrator reset for selected user: %s (UID %u)", users[selectedUser].name,
                     users[selectedUser].credentials.uid);
            view.Add(Gui::Kind::Label, 0, text);
            view.Add(Gui::Kind::Label, 0,
                     "Select the user in Accounts first. Reset uses New/Confirm; current password is not required.");
            resetPassword = view.Add(Gui::Kind::Button, 0, "Reset selected user's password");
        }
        else
        {
            resetPassword = Gui::NoWidget;
        }
    }
    else
    {
        view.Add(Gui::Kind::Label, 0, "Recovery console appearance (clears console; lasts until reboot).");
        view.Add(Gui::Kind::Label, 0,
                 "Changes require foreground ownership of the recovery console. Desktop terminal windows use a fixed "
                 "font/palette.");
        HackosTerminalSettings settings = {};
        syscall_terminal_get(&settings);
        const char* labels[] = {"Font height (8-64 even)", "Background RRGGBBAA", "Text RRGGBBAA"};
        uint32_t values[] = {settings.fontSize, settings.bgColour, settings.fontColour};
        for (unsigned i = 0; i < 3; ++i)
        {
            auto row = Row();
            terminalFields[i] = Field(row, labels[i]);
            char text[32];
            if (i == 0)
            {
                snprintf(text, sizeof(text), "%u", values[i]);
            }
            else
            {
                const char* hex = "0123456789ABCDEF";
                for (unsigned j = 0; j < 8; ++j)
                {
                    text[j] = hex[(values[i] >> ((7 - j) * 4)) & 15];
                }
                text[8] = 0;
            }
            view.SetText(terminalFields[i], text);
            terminalApply[i] = view.Add(Gui::Kind::Button, row, "Apply");
        }
        terminalReset = view.Add(Gui::Kind::Button, 0, "Reset console appearance");
    }
    status = view.Add(Gui::Kind::Label, 0, "Ready");
}

static bool Number(const char* text, unsigned base, uint32_t& value)
{
    value = 0;
    unsigned length = Length(text);
    if (!length || (base == 16 ? length != 8 : length > 2))
    {
        return false;
    }
    for (unsigned i = 0; i < length; ++i)
    {
        unsigned c = text[i];
        unsigned digit = c >= '0' && c <= '9'   ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                : 16;
        if (digit >= base)
        {
            return false;
        }
        value = value * base + digit;
    }
    return true;
}

int main()
{
    if (syscall_identity(&identity))
    {
        return 1;
    }
    Build();
    HackosWindowSpec spec = {120, 80, 1100, 620, "Settings"};
    if (!window.Open(spec))
    {
        return 1;
    }
    Print("SETTINGS_READY\n");
    while (window.Handle())
    {
        Gui::Action action;
        for (unsigned n = 0; n < 16 && window.Next(view, action) == 1; ++n)
        {
            if (action.kind == Gui::ActionKind::Close)
            {
                ClearView();
                window.ReplyClose(true);
                break;
            }
            if (currentTab == 6 && action.kind == Gui::ActionKind::Change)
            {
                selectedUser = view.Get(userList)->selected;
                selectedGroup = view.Get(groupList)->selected;
                if (action.widget == userList || action.widget == groupList)
                {
                    AccountSelection();
                }
            }
            if (action.kind != Gui::ActionKind::Activate)
            {
                continue;
            }
            bool changed = false;
            for (unsigned i = 0; i < 9; ++i)
            {
                if (action.widget == tabs[i])
                {
                    currentTab = i;
                    Build();
                    changed = true;
                    break;
                }
            }
            if (changed)
            {
                continue;
            }
            if (currentTab == 6 && identity.uid == 0 && userCount && groupCount)
            {
                auto uid = users[selectedUser].credentials.uid;
                auto gid = groups[selectedGroup].gid;
                uint64_t result;
                if (action.widget == createUser)
                {
                    result = syscall_user_create(view.Get(userName)->text, gid);
                }
                else if (action.widget == createGroup)
                {
                    result = syscall_group_create(view.Get(groupName)->text);
                }
                else if (action.widget == renameUser || action.widget == renameGroup)
                {
                    bool user = action.widget == renameUser;
                    result = syscall_account_edit(user ? HACKOS_ACCOUNT_RENAME_USER : HACKOS_ACCOUNT_RENAME_GROUP,
                                                  user ? uid : gid, 0, view.Get(user ? userName : groupName)->text);
                }
                else if (action.widget == addGroup)
                {
                    result = syscall_user_add_group(uid, gid);
                }
                else if (action.widget == removeGroup || action.widget == primaryGroup)
                {
                    result = syscall_account_edit(action.widget == primaryGroup ? HACKOS_ACCOUNT_PRIMARY_GROUP
                                                                                : HACKOS_ACCOUNT_REMOVE_GROUP,
                                                  uid, gid, nullptr);
                }
                else
                {
                    continue;
                }
                if (result < HACKOS_NOT_FOUND)
                {
                    Build();
                }
                Message(result);
            }
            else if (currentTab == 7 && (action.widget == changePassword || action.widget == resetPassword))
            {
                const char* secret = view.Get(password)->text;
                if (Length(secret) < 8 || Length(secret) > HACKOS_PASSWORD_MAX ||
                    !Equal(secret, view.Get(confirmation)->text))
                {
                    view.SetText(status, "Passwords must match and contain 8-128 characters.");
                    continue;
                }
                auto result = action.widget == resetPassword
                                  ? syscall_password_set(users[selectedUser].credentials.uid, secret)
                                  : syscall_password_change(view.Get(oldPassword)->text, secret);
                Build();
                Message(result);
                if (!result)
                {
                    view.SetText(status, "Password saved.");
                }
            }
            else if (currentTab == 8)
            {
                if (action.widget == terminalReset)
                {
                    auto result = syscall_terminal_set(HACKOS_TERMINAL_RESET, 0);
                    Build();
                    Message(result);
                }
                for (unsigned i = 0; i < 3; ++i)
                {
                    if (action.widget == terminalApply[i])
                    {
                        uint32_t value;
                        if (!Number(view.Get(terminalFields[i])->text, i ? 16 : 10, value) ||
                            (!i && (value < 8 || value > 64 || value % 2)))
                        {
                            view.SetText(status,
                                         "Use an even font height 8-64, or exactly eight hexadecimal colour digits.");
                            break;
                        }
                        auto result = syscall_terminal_set(i + 1, value);
                        Message(result);
                        if (!result)
                        {
                            view.SetText(status, "Console appearance updated.");
                        }
                    }
                }
            }
        }
        if (!window.Handle())
        {
            break;
        }
        if (view.Dirty() && !window.Paint(view))
        {
            syscall_sleep(16);
        }
        else
        {
            window.Wait();
        }
    }
    return 0;
}
