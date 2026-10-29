#include "hackos/gui.h"
#include "hackos/file_time.h"
#include "hackos/stdio.h"
#include "hackos/user.h"

using namespace User;
static Gui::Window window;
static Gui::View view;
constexpr unsigned PageSize = 128, PreviewRows = 256, PreviewColumns = 48;
static HackosFileInfo entries[PageSize];
static char labels[PageSize][256], preview[PreviewRows][PreviewColumns + 1];
static const char *items[PageSize], *lines[PreviewRows];
static char bytes[HACKOS_FILE_LIMIT], cwd[256], pending[256];
static unsigned count, page, address, listing, status, target, previewList, datesLabel;
static uint64_t editors[8];
static unsigned editButton;
static unsigned openButton, renameButton, deleteButton, previousButton, nextButton;
static bool more, deleting;

static void Failure(uint64_t result)
{
    const char* message = result == HACKOS_DENIED ? "Permission denied. Your account cannot access this path."
                          : result == HACKOS_NOT_FOUND
                              ? "The path no longer exists. Refresh the folder."
                              : "Invalid/existing path, protected, open or read-only file.\nCheck the destination and "
                                "available disk space.\nThe storage device may have reported an I/O error.";
    view.SetText(status, message);
    const char* choices[] = {"OK"};
    view.OpenDialog("File operation failed", message, choices, 1, 0);
}

static bool Selected(char* path)
{
    unsigned selected = view.Get(listing)->selected;
    if (selected >= count)
    {
        view.SetText(status, "Select a file or folder first.");
        return false;
    }
    int size = snprintf(path, 256, "%s%s%s", cwd, Equal(cwd, "/") ? "" : "/", entries[selected].name);
    if (size < 0 || size >= 256)
    {
        view.SetText(status, "Path exceeds the 255-byte limit.");
        return false;
    }
    return true;
}

static void Selection()
{
    unsigned selected = view.Get(listing)->selected;
    bool valid = selected < count;
    bool file = valid && !(entries[selected].attributes & HACKOS_DIRECTORY);
    view.SetEnabled(openButton, valid);
    view.SetEnabled(renameButton, file);
    view.SetEnabled(editButton, file);
    view.SetEnabled(deleteButton, file);
    view.SetText(datesLabel, "");
    if (valid)
    {
        char details[256];
        auto& entry = entries[selected];
        char created[20], modified[20], dates[128];
        FormatFileTime(entry.created, created);
        FormatFileTime(entry.modified, modified);
        snprintf(dates, sizeof(dates), "Created: %s    Modified: %s", created, modified);
        view.SetText(datesLabel, dates);
        snprintf(details, sizeof(details), "%s | %llu bytes | UID %u GID %u | mode %u%u%u%s", entry.name,
                 (unsigned long long)entry.size, entry.uid, entry.gid, (entry.mode >> 6) & 7, (entry.mode >> 3) & 7,
                 entry.mode & 7, file ? "" : " | Folder rename/delete unavailable");
        view.SetText(status, details);
    }
}

static bool Refresh()
{
    count = 0;
    more = false;
    for (unsigned offset = 0; offset < PageSize; offset += HACKOS_READDIR_BATCH_MAX)
    {
        uint64_t got = syscall_readdir_batch(cwd, page * PageSize + offset, entries + offset, HACKOS_READDIR_BATCH_MAX);
        if (got > HACKOS_READDIR_BATCH_MAX)
        {
            count = 0;
            view.SetItems(listing, nullptr, 0);
            view.SetEnabled(nextButton, false);
            view.SetEnabled(previousButton, page != 0);
            Selection();
            Failure(got);
            return false;
        }
        count += got;
        if (got < HACKOS_READDIR_BATCH_MAX)
        {
            break;
        }
    }
    if (count == PageSize)
    {
        HackosFileInfo extra;
        uint64_t got = syscall_readdir(cwd, (page + 1) * PageSize, &extra);
        more = got == 1;
        if (got > 1)
        {
            Failure(got);
        }
    }
    for (unsigned i = 0; i < count; ++i)
    {
        snprintf(labels[i], sizeof(labels[i]), "%s %s", entries[i].attributes & HACKOS_DIRECTORY ? "[DIR]" : "     ",
                 entries[i].name);
        items[i] = labels[i];
    }
    view.SetItems(listing, items, count);
    view.SetEnabled(previousButton, page != 0);
    view.SetEnabled(nextButton, more);
    view.SetText(address, cwd);
    Selection();
    if (!count)
    {
        view.SetText(status, "Empty folder/page. Use Previous or Refresh if entries changed.");
    }
    return true;
}

static void Navigate(const char* path)
{
    // Probe read permission before changing this process's working directory.
    HackosFileInfo probe;
    uint64_t result = syscall_readdir(path, 0, &probe);
    if (result > 1)
    {
        Failure(result);
        return;
    }
    result = syscall_chdir(path);
    if (result)
    {
        Failure(result);
        return;
    }
    if (syscall_getcwd(cwd, sizeof(cwd)))
    {
        Failure(HACKOS_ERROR);
        return;
    }
    page = 0;
    view.SetItems(previewList, nullptr, 0);
    Refresh();
}

static void Open()
{
    char path[256];
    if (!Selected(path))
    {
        return;
    }
    HackosFileInfo info;
    uint64_t result = syscall_stat(path, &info);
    if (result)
    {
        Failure(result);
        return;
    }
    if (info.attributes & HACKOS_DIRECTORY)
    {
        Navigate(path);
        return;
    }
    auto fd = syscall_fd_open(path, HACKOS_OPEN_READ);
    if (fd >= HACKOS_FD_LIMIT)
    {
        Failure(fd);
        return;
    }
    unsigned length = 0;
    while (length < sizeof(bytes))
    {
        unsigned amount = sizeof(bytes) - length;
        if (amount > HACKOS_IO_LIMIT)
        {
            amount = HACKOS_IO_LIMIT;
        }
        auto got = syscall_fd_read(fd, bytes + length, amount, HACKOS_WAIT_FOREVER);
        if (got > amount)
        {
            syscall_fd_close(fd);
            Failure(got);
            return;
        }
        if (!got)
        {
            break;
        }
        length += got;
    }
    syscall_fd_close(fd);
    bool binary = false;
    for (unsigned i = 0; i < length; ++i)
    {
        unsigned char c = bytes[i];
        if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c > 126)
        {
            binary = true;
        }
    }
    unsigned row = 0, column = 0, consumed = 0;
    for (; consumed < length && row < PreviewRows; ++consumed)
    {
        unsigned char c = bytes[consumed];
        if (binary)
        {
            const char* hex = "0123456789ABCDEF";
            preview[row][column++] = hex[c >> 4];
            preview[row][column++] = hex[c & 15];
            preview[row][column++] = ' ';
        }
        else if (c != '\n' && c != '\r')
        {
            preview[row][column++] = c == '\t' ? ' ' : c;
        }
        if (column >= PreviewColumns || (!binary && c == '\n'))
        {
            preview[row][column] = 0;
            lines[row] = preview[row];
            ++row;
            column = 0;
        }
    }
    if (row < PreviewRows && (column || !row))
    {
        preview[row][column] = 0;
        lines[row] = preview[row];
        ++row;
    }
    view.SetItems(previewList, lines, row);
    char message[256];
    snprintf(message, sizeof(message), "%s preview: %s%s", binary ? "Hex" : "Read-only text", info.name,
             consumed < length || info.size > length ? " (truncated)" : "");
    view.SetText(status, message);
}

int main(int argc, char** argv)
{
    view.Init();
    auto navigation = view.Add(Gui::Kind::Row, 0);
    view.SetLayout(navigation, 0, 32, 0, 0, 6);
    auto up = view.Add(Gui::Kind::Button, navigation, "Up");
    address = view.Add(Gui::Kind::TextField, navigation, "/");
    view.SetLayout(address, 0, 32, 1);
    auto go = view.Add(Gui::Kind::Button, navigation, "Go");
    auto refresh = view.Add(Gui::Kind::Button, navigation, "Refresh");
    auto toolbar = view.Add(Gui::Kind::Row, 0);
    view.SetLayout(toolbar, 0, 32, 0, 0, 6);
    openButton = view.Add(Gui::Kind::Button, toolbar, "Open");
    editButton = view.Add(Gui::Kind::Button, toolbar, "Edit text");
    previousButton = view.Add(Gui::Kind::Button, toolbar, "Previous");
    nextButton = view.Add(Gui::Kind::Button, toolbar, "Next");
    deleteButton = view.Add(Gui::Kind::Button, toolbar, "Delete file");
    auto body = view.Add(Gui::Kind::Row, 0);
    view.SetLayout(body, 0, 0, 1, 0, 8);
    listing = view.Add(Gui::Kind::List, body);
    view.SetLayout(listing, 0, 0, 1);
    previewList = view.Add(Gui::Kind::List, body);
    view.SetLayout(previewList, 0, 0, 1);
    view.Add(Gui::Kind::Label, 0, "Name or destination path (relative to this folder, or absolute):");
    auto mutations = view.Add(Gui::Kind::Row, 0);
    view.SetLayout(mutations, 0, 32, 0, 0, 6);
    target = view.Add(Gui::Kind::TextField, mutations);
    view.SetLayout(target, 0, 32, 1);
    auto mkdirButton = view.Add(Gui::Kind::Button, mutations, "New folder");
    renameButton = view.Add(Gui::Kind::Button, mutations, "Rename / Move file");
    datesLabel = view.Add(Gui::Kind::Label, 0, "");
    status = view.Add(Gui::Kind::Label, 0, "Select an entry and choose Open. File previews are read-only.");
    HackosWindowSpec spec = {60, 60, 1000, 600, "Files"};
    if (!window.Open(spec))
    {
        return 1;
    }
    if (syscall_getcwd(cwd, sizeof(cwd)))
    {
        window.Close();
        return 1;
    }
    Navigate(argc > 1 ? argv[1] : "/");
    Print("FILES_READY\n");
    while (window.Handle())
    {
        bool children = false;
        for (auto& child : editors)
        {
            int64_t code;
            if (child && syscall_wait(child, &code, HACKOS_WAIT_NOHANG) == child)
            {
                child = 0;
                if (code)
                {
                    view.SetText(status, "Editor exited with an error.");
                }
            }
            children = children || child != 0;
        }
        Gui::Action action;
        for (unsigned n = 0; n < 16; ++n)
        {
            auto event = window.Next(view, action);
            if (event == HACKOS_ERROR)
            {
                window.Close();
                break;
            }
            if (event != 1)
            {
                break;
            }
            if (action.kind == Gui::ActionKind::Close)
            {
                window.ReplyClose(true);
                break;
            }
            if (action.kind == Gui::ActionKind::Dialog && deleting)
            {
                deleting = false;
                if (action.value == 0)
                {
                    auto result = syscall_unlink(pending);
                    if (result)
                    {
                        Failure(result);
                    }
                    else
                    {
                        Refresh();
                        view.SetItems(previewList, nullptr, 0);
                        view.SetText(status, "File deleted.");
                    }
                }
            }
            if (action.kind == Gui::ActionKind::Change && action.widget == listing)
            {
                Selection();
            }
            if (action.kind != Gui::ActionKind::Activate)
            {
                continue;
            }
            if (action.widget == go)
            {
                Navigate(view.Get(address)->text);
            }
            else if (action.widget == up)
            {
                Navigate("..");
            }
            else if (action.widget == refresh || action.widget == previousButton || action.widget == nextButton)
            {
                page = action.widget == refresh          ? 0
                       : action.widget == previousButton ? (page ? page - 1 : 0)
                                                         : page + 1;
                Refresh();
            }
            else if (action.widget == openButton)
            {
                Open();
            }
            else if (action.widget == editButton)
            {
                char path[256];
                if (!Selected(path))
                {
                    continue;
                }
                bool launched = false;
                for (auto& child : editors)
                {
                    if (!child)
                    {
                        const char* args[] = {"editor", path};
                        auto pid = syscall_spawn_args("~/USR/BIN/EDITOR", 2, args);
                        if (pid < HACKOS_NOT_FOUND)
                        {
                            child = pid;
                            children = launched = true;
                        }
                        break;
                    }
                }
                view.SetText(status,
                             launched ? "Opened in Editor." : "Cannot launch Editor (resource limit or permissions).");
            }
            else if (action.widget == deleteButton && Selected(pending))
            {
                const char* choices[] = {"Delete", "Cancel"};
                deleting = view.OpenDialog("Permanently delete file?", pending, choices, 2, 1);
            }
            else if (action.widget == mkdirButton || action.widget == renameButton)
            {
                const char* destination = view.Get(target)->text;
                if (!*destination)
                {
                    view.SetText(status, "Enter a name or destination path first.");
                    view.SetFocus(target);
                    continue;
                }
                char source[256];
                if (action.widget == renameButton && !Selected(source))
                {
                    continue;
                }
                auto result =
                    action.widget == mkdirButton ? syscall_mkdir(destination) : syscall_rename(source, destination);
                if (result)
                {
                    Failure(result);
                }
                else
                {
                    Refresh();
                    view.SetText(target, "");
                    view.SetItems(previewList, nullptr, 0);
                    view.SetText(status, "Operation completed.");
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
            window.Wait(children ? 100 : HACKOS_WAIT_FOREVER);
        }
    }
    return 0;
}
