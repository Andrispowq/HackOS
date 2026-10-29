#include "hackos/gui.h"
#include "hackos/stdio.h"
#include "hackos/user.h"
#include "hackos/runtime/path.h"

using namespace User;
static Gui::Window window;
static Gui::View view, picker;
static Gui::TextDocument document;
static char scratch[HACKOS_FILE_LIMIT], currentPath[256], directory[256], savePath[256];
static HackosFileInfo entries[64];
static const char* names[64];
static unsigned area, pathLabel, status, pickerDirectory, pickerPath, pickerList, pickerAccept, pickerStatus;
static unsigned entryCount, page;
static bool picking, saving, overwrite, pendingClose;
enum class Pending
{
    None,
    New,
    Open,
    Close
};
static Pending pending;

static Gui::View& Active()
{
    return picking ? picker : view;
}

static void ErrorDialog(const char* message)
{
    const char* labels[] = {"OK"};
    Active().OpenDialog("Editor", message, labels, 1, 0);
}

static void Status()
{
    char title[256], message[128];
    snprintf(title, sizeof(title), "%s%s", document.modified ? "* " : "", *currentPath ? currentPath : "Untitled");
    view.SetText(pathLabel, title);
    unsigned line = 1;
    for (unsigned i = 0; i < document.cursor; ++i)
    {
        if (document.text[i] == '\n')
        {
            ++line;
        }
    }
    snprintf(message, sizeof(message), "Line %u, column %u | %u / 32768 bytes%s", line,
             document.cursor - document.Start(document.cursor) + 1, document.length,
             document.modified ? " | Unsaved changes" : " | Saved");
    view.SetText(status, message);
}

static void ListDirectory(const char* input)
{
    char path[256];
    if (!NormalizePath(input, directory, path))
    {
        ErrorDialog("Invalid or oversized folder path.");
        return;
    }
    entryCount = 0;
    for (unsigned offset = 0; offset < 64; offset += HACKOS_READDIR_BATCH_MAX)
    {
        auto count = syscall_readdir_batch(path, page * 64 + offset, entries + offset, HACKOS_READDIR_BATCH_MAX);
        if (count > HACKOS_READDIR_BATCH_MAX)
        {
            entryCount = 0;
            picker.SetItems(pickerList, nullptr, 0);
            ErrorDialog(count == HACKOS_DENIED ? "Permission denied reading this folder." : "Cannot read this folder.");
            return;
        }
        entryCount += count;
        if (count < HACKOS_READDIR_BATCH_MAX)
        {
            break;
        }
    }
    Copy(directory, path);
    picker.SetText(pickerDirectory, directory);
    for (unsigned i = 0; i < entryCount; ++i)
    {
        names[i] = entries[i].name;
    }
    picker.SetItems(pickerList, names, entryCount);
    picker.SetText(pickerStatus, entryCount ? "Select a file, or use Browse selection to enter a folder."
                                            : "Empty page. Use Previous, or choose another folder.");
}

static void ShowPicker(bool save)
{
    picking = true;
    saving = save;
    overwrite = false;
    page = 0;
    picker.SetText(pickerAccept, save ? "Save" : "Open");
    picker.SetText(pickerPath, *currentPath ? currentPath : "");
    ListDirectory(directory);
    picker.SetFocus(pickerPath);
    picker.Invalidate();
}

static void Continue()
{
    Pending operation = pending;
    pending = Pending::None;
    if (operation == Pending::Close)
    {
        if (pendingClose)
        {
            window.ReplyClose(true);
        }
        else
        {
            window.Close();
        }
    }
    else if (operation == Pending::New)
    {
        document.Load("", 0);
        currentPath[0] = 0;
        view.SetFocus(area);
        Status();
    }
    else if (operation == Pending::Open)
    {
        ShowPicker(false);
    }
}

static void CancelPending()
{
    pending = Pending::None;
    if (pendingClose)
    {
        window.ReplyClose(false);
        pendingClose = false;
    }
}

static bool Save(const char* path, bool create)
{
    auto result = syscall_file_save(path, document.text, document.length, create ? HACKOS_SAVE_CREATE : 0);
    if (result)
    {
        ErrorDialog(result == HACKOS_DENIED ? "Permission denied. Your changes are still in memory."
                                            : "Cannot save: protected/open file, invalid path, full disk\nor I/O "
                                              "error. Your changes are still in memory.");
        CancelPending();
        return false;
    }
    Copy(currentPath, path);
    document.modified = false;
    picking = false;
    view.SetFocus(area);
    view.Invalidate();
    Status();
    Continue();
    return true;
}

static void SaveCurrent()
{
    if (!*currentPath)
    {
        ShowPicker(true);
    }
    else
    {
        Save(currentPath, false);
    }
}

static void Request(Pending operation)
{
    pending = operation;
    if (!document.modified)
    {
        Continue();
        return;
    }
    const char* labels[] = {"Save", "Discard", "Cancel"};
    view.OpenDialog("Unsaved changes", "Save this document before continuing?", labels, 3, 2);
}

static bool Open(const char* path)
{
    HackosFileInfo info;
    auto result = syscall_stat(path, &info);
    if (result || (info.attributes & HACKOS_DIRECTORY) || info.size > HACKOS_FILE_LIMIT)
    {
        ErrorDialog(result == HACKOS_DENIED ? "Permission denied opening this file."
                                            : "Open requires a readable text file of at most 32768 bytes.");
        return false;
    }
    auto fd = syscall_fd_open(path, HACKOS_OPEN_READ);
    if (fd >= HACKOS_FD_LIMIT)
    {
        ErrorDialog(fd == HACKOS_DENIED ? "Permission denied opening this file." : "Cannot open this file.");
        return false;
    }
    unsigned length = 0;
    bool good = true;
    while (length < sizeof(scratch))
    {
        unsigned amount = sizeof(scratch) - length;
        if (amount > HACKOS_IO_LIMIT)
        {
            amount = HACKOS_IO_LIMIT;
        }
        auto got = syscall_fd_read(fd, scratch + length, amount, HACKOS_WAIT_FOREVER);
        if (got > amount)
        {
            good = false;
            break;
        }
        if (!got)
        {
            break;
        }
        length += got;
    }
    char extra;
    good = good && syscall_fd_read(fd, &extra, 1, HACKOS_WAIT_FOREVER) == 0;
    syscall_fd_close(fd);
    if (!good || !document.Load(scratch, length))
    {
        ErrorDialog("Cannot load: read error, oversized file or unsupported\nbytes. ASCII, LF and tab are supported. "
                    "Buffer kept.");
        return false;
    }
    Copy(currentPath, path);
    picking = false;
    pending = Pending::None;
    view.SetFocus(area);
    view.Invalidate();
    Status();
    return true;
}

int main(int argc, char** argv)
{
    view.Init();
    auto toolbar = view.Add(Gui::Kind::Row, 0);
    view.SetLayout(toolbar, 0, 32, 0, 0, 6);
    auto newButton = view.Add(Gui::Kind::Button, toolbar, "New");
    auto openButton = view.Add(Gui::Kind::Button, toolbar, "Open...");
    auto saveButton = view.Add(Gui::Kind::Button, toolbar, "Save");
    auto saveAsButton = view.Add(Gui::Kind::Button, toolbar, "Save as...");
    pathLabel = view.Add(Gui::Kind::Label, 0, "Untitled");
    area = view.Add(Gui::Kind::TextArea, 0);
    view.SetLayout(area, 0, 0, 1, 0, 0);
    view.SetDocument(area, &document);
    view.Add(Gui::Kind::Label, 0, "Ctrl-N/O/S: new/open/save | Ctrl-A/C/X/V: select/copy/cut/paste | Ctrl-I: tab");
    status = view.Add(Gui::Kind::Label, 0, "");
    view.SetFocus(area);
    picker.Init();
    picker.Add(Gui::Kind::Label, 0, "Choose a file (ASCII text, up to 32 KiB)");
    auto navigation = picker.Add(Gui::Kind::Row, 0);
    picker.SetLayout(navigation, 0, 32, 0, 0, 6);
    auto up = picker.Add(Gui::Kind::Button, navigation, "Up");
    pickerDirectory = picker.Add(Gui::Kind::TextField, navigation, "~");
    picker.SetLayout(pickerDirectory, 0, 32, 1);
    auto go = picker.Add(Gui::Kind::Button, navigation, "Go");
    auto browse = picker.Add(Gui::Kind::Button, navigation, "Browse selection");
    auto previous = picker.Add(Gui::Kind::Button, navigation, "Previous");
    auto next = picker.Add(Gui::Kind::Button, navigation, "Next");
    pickerList = picker.Add(Gui::Kind::List, 0);
    picker.SetLayout(pickerList, 0, 0, 1);
    pickerStatus = picker.Add(Gui::Kind::Label, 0, "");
    picker.Add(Gui::Kind::Label, 0, "File path (relative to this folder, or absolute):");
    pickerPath = picker.Add(Gui::Kind::TextField, 0);
    auto actions = picker.Add(Gui::Kind::Row, 0);
    picker.SetLayout(actions, 0, 32, 0, 0, 6);
    pickerAccept = picker.Add(Gui::Kind::Button, actions, "Open");
    auto cancel = picker.Add(Gui::Kind::Button, actions, "Cancel");
    if (syscall_getcwd(directory, sizeof(directory)))
    {
        Copy(directory, "~");
    }
    HackosWindowSpec spec = {120, 80, 1000, 640, "Text editor"};
    if (!window.Open(spec))
    {
        return 1;
    }
    if (argc > 1)
    {
        char path[256];
        if (!NormalizePath(argv[1], directory, path))
        {
            ErrorDialog("Invalid file path.");
        }
        else
        {
            Open(path);
        }
    }
    Status();
    Print("EDITOR_READY\n");
    while (window.Handle())
    {
        Gui::Action action;
        for (unsigned n = 0; n < 16; ++n)
        {
            auto result = window.Next(Active(), action);
            if (result == HACKOS_ERROR)
            {
                window.Close();
                break;
            }
            if (result != 1)
            {
                break;
            }
            if (action.kind == Gui::ActionKind::Close)
            {
                if (Active().DialogOpen() || pendingClose)
                {
                    window.ReplyClose(false);
                }
                else
                {
                    picking = false;
                    view.Invalidate();
                    pendingClose = true;
                    Request(Pending::Close);
                }
            }
            else if (action.kind == Gui::ActionKind::Dialog)
            {
                if (picking && overwrite)
                {
                    overwrite = false;
                    if (action.value == 0)
                    {
                        Save(savePath, false);
                    }
                }
                else if (!picking && pending != Pending::None)
                {
                    if (action.value == 0)
                    {
                        SaveCurrent();
                    }
                    else if (action.value == 1)
                    {
                        Continue();
                    }
                    else
                    {
                        CancelPending();
                    }
                }
            }
            else if (picking)
            {
                if (action.kind == Gui::ActionKind::Change && action.widget == pickerList)
                {
                    unsigned selected = picker.Get(pickerList)->selected;
                    if (selected < entryCount)
                    {
                        char path[256];
                        if (NormalizePath(entries[selected].name, directory, path))
                        {
                            picker.SetText(pickerPath, path);
                            picker.SetText(pickerStatus,
                                           entries[selected].attributes & HACKOS_DIRECTORY ? "Folder" : "File");
                        }
                    }
                }
                if (action.kind != Gui::ActionKind::Activate)
                {
                    continue;
                }
                if (action.widget == cancel)
                {
                    picking = false;
                    CancelPending();
                    view.Invalidate();
                }
                else if (action.widget == up || action.widget == go || action.widget == previous ||
                         action.widget == next)
                {
                    page = action.widget == previous ? (page ? page - 1 : 0) : action.widget == next ? page + 1 : 0;
                    ListDirectory(action.widget == up   ? ".."
                                  : action.widget == go ? picker.Get(pickerDirectory)->text
                                                        : directory);
                }
                else if (action.widget == browse && picker.Get(pickerList)->selected < entryCount)
                {
                    auto& entry = entries[picker.Get(pickerList)->selected];
                    if (entry.attributes & HACKOS_DIRECTORY)
                    {
                        page = 0;
                        ListDirectory(entry.name);
                    }
                }
                else if (action.widget == pickerAccept)
                {
                    char path[256];
                    if (!NormalizePath(picker.Get(pickerPath)->text, directory, path))
                    {
                        ErrorDialog("Enter a valid file path.");
                        continue;
                    }
                    if (!saving)
                    {
                        Open(path);
                    }
                    else
                    {
                        HackosFileInfo info;
                        auto found = syscall_stat(path, &info);
                        if (found == HACKOS_NOT_FOUND)
                        {
                            Save(path, true);
                        }
                        else if (found || (info.attributes & HACKOS_DIRECTORY))
                        {
                            ErrorDialog("Cannot save to this path. Check permissions and filename.");
                        }
                        else
                        {
                            Copy(savePath, path);
                            const char* labels[] = {"Replace", "Cancel"};
                            overwrite = picker.OpenDialog("Replace existing file?", "The selected file already exists.",
                                                          labels, 2, 1);
                        }
                    }
                }
            }
            else if (action.kind == Gui::ActionKind::Activate || action.kind == Gui::ActionKind::Command)
            {
                bool command = action.kind == Gui::ActionKind::Command;
                if ((!command && action.widget == newButton) || (command && action.value == 0x31))
                {
                    Request(Pending::New);
                }
                else if ((!command && action.widget == openButton) || (command && action.value == 0x18))
                {
                    Request(Pending::Open);
                }
                else if ((!command && action.widget == saveButton) || (command && action.value == 0x1f))
                {
                    SaveCurrent();
                }
                else if (!command && action.widget == saveAsButton)
                {
                    ShowPicker(true);
                }
            }
            if (!picking)
            {
                Status();
            }
            if (!window.Handle())
            {
                break;
            }
        }
        if (!window.Handle())
        {
            break;
        }
        if (Active().Dirty() && !window.Paint(Active()))
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
