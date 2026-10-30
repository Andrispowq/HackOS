#include "proc/user/terminals.h"
#include "syscall.h"
#include "hackos/syscall.h"
#include "hackos/runtime/path.h"
#include "arch/x86_64/interrupts/guard.h"
#include "arch/x86_64/timer/rtc.h"
#include "arch/x86_64/timer/pit.h"
#include "arch/x86_64/ports.h"
#include "proc/tasking/process.h"
#include "proc/user/user.h"
#include "proc/user/streams.h"
#include "proc/user/hardware.h"
#include "net/ping.h"
#include "net/dns.h"
#include "net/socket.h"
#include "net/tcp.h"
#include "proc/user/accounts.h"
#include "proc/user/account_store.h"
#include "security/crypto.h"
#include "proc/user/windows.h"
#include "proc/user/address_space.h"
#include "drivers/keyboard/keyboard.h"
#include "drivers/screen/screen.h"
#include "fs/filesystem.h"
#include "fs/permissions.h"
#include "init/kernelInit.h"
#include "acpi/fadt.h"
#include "lib/stdio.h"
#include "lib/memory.h"
#include "lib/string.h"

extern Process* ready_queue;
extern KernelInfo* kInfo;
static uint64_t nextHandle;

static Process* Caller()
{
    return GetRunningProcess();
}

static bool IsRoot()
{
    return Caller()->credentials.uid == HACKOS_ROOT_UID;
}

static bool Path(const char* address, char (&path)[256])
{
    char input[256];
    if (!Caller()->userSpace->CopyString(input, (uint64_t)address, sizeof(input)) || !filesystems[0] ||
        !NormalizePath(input, Caller()->working_dir, path))
    {
        return false;
    }

    // Credential records are kernel-only even for UID 0. No file handle can
    // expose a password verifier or a raw account-store slot to userspace.
    return strcmp(path, (char*)"~/SYSTEM/ACCT0.DAT") != 0 && strcmp(path, (char*)"~/SYSTEM/ACCT1.DAT") != 0;
}

static bool Output(void* destination, const void* source, uint64_t length)
{
    return Caller()->userSpace->CopyToUser((uint64_t)destination, source, length);
}

static void Name(char (&destination)[256], const char* source)
{
    uint64_t i = 0;
    while (i < 255 && source[i])
    {
        destination[i] = source[i];
        ++i;
    }

    destination[i] = 0;
}

static void Info(HackosFileInfo& info, ActiveFile* file)
{
    info.size = file->GetSize();
    info.created = file->GetCreated();
    info.modified = file->GetModified();
    info.attributes = file->GetAttributes();
    Name(info.name, file->GetName());
    auto permissions = file->GetPermissions();
    info.uid = permissions.uid;
    info.gid = permissions.gid;
    info.mode = permissions.mode;
    info.metadataVersion = 1;
}

static uint64_t UserInput()
{
    return ReadTerminal(HACKOS_INPUT_NONBLOCK);
}

static uint64_t UserConsole(uint64_t operation)
{
    if (!OwnsTerminal(Caller()->pid))
    {
        return HACKOS_DENIED;
    }

    if (operation == 0)
    {
        TerminalPut('\b');
    }
    else if (operation == 1)
    {
        TerminalClear();
    }
    else if (operation != 2) // Query foreground ownership without consuming input.
    {
        return HACKOS_ERROR;
    }

    return 0;
}

static uint64_t UserTerminalCursor(uint64_t back, uint64_t visible)
{
    if (!OwnsTerminal(Caller()->pid))
    {
        return HACKOS_DENIED;
    }
    if (back > HACKOS_TERMINAL_CURSOR_MAX || visible > 1)
    {
        return HACKOS_ERROR;
    }
    auto tty = ProcessTerminal(Caller());
    if (tty)
    {
        tty->caretBack = back;
        tty->caret = visible;
        tty->cursorMode = 0;
        ++tty->revision;
    }
    else
    {
        Display::SharedDisplay()->SetCaret(Caller()->pid, back, visible);
    }
    return 0;
}

static uint64_t UserTerminalGet(HackosTerminalSettings* destination)
{
    HackosTerminalSettings settings = Display::SharedDisplay()->console.Settings();
    auto tty = ProcessTerminal(Caller());
    if (tty)
    {
        settings = tty->appearance.Settings(tty->columns, tty->rows);
    }
    return Output(destination, &settings, sizeof(settings)) ? 0 : HACKOS_ERROR;
}

static uint64_t UserTerminalSet(uint64_t key, uint64_t value)
{
    if (!OwnsTerminal(Caller()->pid))
    {
        return HACKOS_DENIED;
    }
    auto tty = ProcessTerminal(Caller());
    if (tty)
    {
        return ConfigureWindowTerminal(*tty, key, value) ? 0 : HACKOS_ERROR;
    }
    return Display::SharedDisplay()->ConfigureTerminal(key, value) ? 0 : HACKOS_ERROR;
}

static uint64_t UserClock(HackosClock* destination)
{
    struct tm time = {};
    if (!TryGetTimeRTC(&time))
    {
        return HACKOS_ERROR;
    }
    HackosClock value = {tick,
                         (uint8_t)time.second,
                         (uint8_t)time.minute,
                         (uint8_t)time.hour,
                         (uint8_t)time.day,
                         (uint8_t)(time.month + 1),
                         0,
                         (uint16_t)(time.year + 1900)};
    return Output(destination, &value, sizeof(value)) ? 0 : HACKOS_ERROR;
}

static uint64_t UserProcessInfo(uint64_t index, HackosProcessInfo* destination)
{
    auto p = ready_queue;
    while (p && index)
    {
        p = p->next;
        --index;
    }

    if (!p)
    {
        return 0;
    }

    HackosProcessInfo info = {};
    info.pid = p->pid;
    info.state = p->state;
    info.uid = p->credentials.uid;
    info.gid = p->credentials.gid;
    info.parentPid = p->parentPid;
    info.processGroup = p->processGroup;
    Name(info.name, p->name);
    return Output(destination, &info, sizeof(info)) ? 1 : HACKOS_ERROR;
}

static uint64_t UserKill(uint64_t pid)
{
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->pid == pid)
        {
            if (!p->userSpace || p->systemShell || p == Caller() ||
                (!IsRoot() && Caller()->credentials.uid != p->credentials.uid))
            {
                return HACKOS_DENIED;
            }

            if (p->state == PROCESS_STATE_ZOMBIE)
            {
                return HACKOS_ERROR;
            }

            Kill(pid);
            return 0;
        }
    }

    return HACKOS_ERROR;
}

static uint64_t UserSpawn(const char* name)
{
    if (Caller()->authenticationAgent)
    {
        return HACKOS_DENIED;
    }

    char path[256];
    if (!Path(name, path))
    {
        return HACKOS_ERROR;
    }

    unsigned count = 0;
    for (auto p = ready_queue; p; p = p->next)
    {
        ++count;
    }

    if (count >= HACKOS_PROCESS_LIMIT)
    {
        return HACKOS_ERROR;
    }

    uint64_t pid = LoadUserProgramTask(path);
    return pid ? pid : HACKOS_ERROR;
}

static uint64_t UserSpawnArgs(const char* name, uint64_t argc, const char* const* argv,
                              const HackosCredentials* credentials = nullptr)
{
    if (Caller()->authenticationAgent)
    {
        return HACKOS_DENIED;
    }

    char path[256], strings[HACKOS_ARG_BYTES];
    uint64_t pointers[HACKOS_MAX_ARGS];
    const char* arguments[HACKOS_MAX_ARGS];
    auto space = Caller()->userSpace;
    if (!Path(name, path) || !argc || argc > HACKOS_MAX_ARGS ||
        !space->CopyFromUser(pointers, (uint64_t)argv, argc * sizeof(uint64_t)))
    {
        return HACKOS_ERROR;
    }

    uint64_t used = 0;
    for (uint64_t i = 0; i < argc; ++i)
    {
        if (!space->CopyString(strings + used, pointers[i], sizeof(strings) - used))
        {
            return HACKOS_ERROR;
        }

        arguments[i] = strings + used;
        while (strings[used++])
        {
        }
    }

    unsigned count = 0;
    for (auto p = ready_queue; p; p = p->next)
    {
        ++count;
    }

    if (count >= HACKOS_PROCESS_LIMIT)
    {
        return HACKOS_ERROR;
    }

    uint64_t pid = LoadUserProgramTask(path, false, argc, arguments, credentials);
    return pid ? pid : HACKOS_ERROR;
}

static void GiveTerminal(uint64_t pid)
{
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->pid == pid)
        {
            p->terminalReturnPid = Caller()->pid;
            SetTerminalOwner(pid);
            return;
        }
    }
}

static uint64_t UserSpawnForeground(const char* name, uint64_t argc, const char* const* argv)
{
    if (!OwnsTerminal(Caller()->pid))
    {
        return HACKOS_DENIED;
    }

    // Dispatcher holds a PreemptionGuard: the child cannot run before handoff.
    uint64_t pid = UserSpawnArgs(name, argc, argv);
    if (pid != HACKOS_ERROR)
    {
        GiveTerminal(pid);
    }

    return pid;
}

static uint64_t UserSpawnJob(const char* name, uint64_t argc, const char* const* argv, uint64_t flags)
{
    if (flags & ~HACKOS_SPAWN_FOREGROUND)
    {
        return HACKOS_ERROR;
    }

    if ((flags & HACKOS_SPAWN_FOREGROUND) && !OwnsTerminal(Caller()->pid))
    {
        return HACKOS_DENIED;
    }

    uint64_t pid = UserSpawnArgs(name, argc, argv);
    if (pid == HACKOS_ERROR || pid == HACKOS_DENIED)
    {
        return pid;
    }

    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->pid == pid)
        {
            p->processGroup = pid;
            p->jobOwnerPid = Caller()->pid;
        }
    }

    for (auto& result : Caller()->children)
    {
        if (result.pid == pid)
        {
            result.job = true;
        }
    }

    if (flags & HACKOS_SPAWN_FOREGROUND)
    {
        GiveTerminal(pid);
    }

    return pid;
}

static uint64_t UserSpawnAs(const char* name, uint64_t argc, const char* const* argv, uint64_t uid, uint64_t flags)
{
    if (!IsRoot() || ((flags & HACKOS_SPAWN_FOREGROUND) && !OwnsTerminal(Caller()->pid)))
    {
        return HACKOS_DENIED;
    }

    const auto credentials = accounts.Credentials(uid);
    if (!credentials || (flags & ~HACKOS_SPAWN_FOREGROUND))
    {
        return HACKOS_ERROR;
    }

    uint64_t pid = UserSpawnArgs(name, argc, argv, credentials);
    if (pid != HACKOS_ERROR && (flags & HACKOS_SPAWN_FOREGROUND))
    {
        GiveTerminal(pid);
    }

    return pid;
}

static uint64_t UserIdentity(HackosCredentials* destination)
{
    return Output(destination, &Caller()->credentials, sizeof(*destination)) ? 0 : HACKOS_ERROR;
}

static uint64_t UserAccountInfo(uint64_t index, HackosUserInfo* destination)
{
    const auto value = accounts.User(index);
    return !value ? 0 : Output(destination, value, sizeof(*value)) ? 1 : HACKOS_ERROR;
}

static uint64_t UserGroupInfo(uint64_t index, HackosGroupInfo* destination)
{
    const auto value = accounts.Group(index);
    return !value ? 0 : Output(destination, value, sizeof(*value)) ? 1 : HACKOS_ERROR;
}

static uint64_t SaveAccounts(uint64_t result, const AccountRegistry& previous)
{
    if (result == HACKOS_ERROR)
    {
        return result;
    }

    if (!accountStore.Commit(accounts))
    {
        accounts = previous;
        return HACKOS_ERROR;
    }

    return result;
}

static uint64_t UserCreateAccount(const char* name, uint64_t gid)
{
    if (!IsRoot())
    {
        return HACKOS_DENIED;
    }

    char copy[HACKOS_ACCOUNT_NAME];
    if (accountStore.Status() != AccountStore::Ready ||
        !Caller()->userSpace->CopyString(copy, (uint64_t)name, sizeof(copy)))
    {
        return HACKOS_ERROR;
    }

    AccountRegistry previous = accounts;
    return SaveAccounts(accounts.CreateUser(copy, gid), previous);
}

static uint64_t UserCreateGroup(const char* name)
{
    if (!IsRoot())
    {
        return HACKOS_DENIED;
    }

    char copy[HACKOS_ACCOUNT_NAME];
    if (accountStore.Status() != AccountStore::Ready ||
        !Caller()->userSpace->CopyString(copy, (uint64_t)name, sizeof(copy)))
    {
        return HACKOS_ERROR;
    }

    AccountRegistry previous = accounts;
    return SaveAccounts(accounts.CreateGroup(copy), previous);
}

static uint64_t UserAddGroup(uint64_t uid, uint64_t gid)
{
    if (!IsRoot())
    {
        return HACKOS_DENIED;
    }

    if (accountStore.Status() != AccountStore::Ready)
    {
        return HACKOS_ERROR;
    }

    AccountRegistry previous = accounts;
    return SaveAccounts(accounts.AddGroup(uid, gid), previous);
}

static uint64_t UserAccountEdit(uint64_t operation, uint64_t id, uint64_t value, const char* name)
{
    if (!IsRoot())
    {
        return HACKOS_DENIED;
    }
    char copy[HACKOS_ACCOUNT_NAME] = {};
    if (accountStore.Status() != AccountStore::Ready ||
        ((operation == HACKOS_ACCOUNT_RENAME_USER || operation == HACKOS_ACCOUNT_RENAME_GROUP) &&
         !Caller()->userSpace->CopyString(copy, (uint64_t)name, sizeof(copy))))
    {
        return HACKOS_ERROR;
    }
    AccountRegistry previous = accounts;
    return SaveAccounts(accounts.Edit(operation, id, value, copy), previous);
}

static bool CopyPassword(const char* source, char (&password)[HACKOS_PASSWORD_MAX + 1])
{
    if (!Caller()->userSpace->CopyString(password, (uint64_t)source, sizeof(password)))
    {
        return false;
    }

    uint64_t length = strlen(password);
    if (length < 8 || length > HACKOS_PASSWORD_MAX)
    {
        return false;
    }

    for (unsigned i = 0; i < length; ++i)
    {
        if (password[i] < 32 || password[i] > 126)
        {
            return false;
        }
    }

    return true;
}

static uint64_t NewPassword(const char* source, PasswordRecord& record)
{
    char password[HACKOS_PASSWORD_MAX + 1] = {};
    bool valid = CopyPassword(source, password);
    if (!valid || !RandomSalt(record.salt))
    {
        SecureErase(password, sizeof(password));
        return valid ? HACKOS_NO_ENTROPY : HACKOS_ERROR;
    }

    record.enabled = 1;
    record.iterations = PASSWORD_ITERATIONS;
    Pbkdf2Sha256(password, strlen(password), record.salt, sizeof(record.salt), record.iterations, record.digest);
    SecureErase(password, sizeof(password));
    return 0;
}

static uint64_t UserAuthState()
{
    return Caller()->authenticationAgent ? (uint64_t)accountStore.Status() : HACKOS_DENIED;
}

static uint64_t UserAuthSetup(const char* username, const char* password)
{
    if (!Caller()->authenticationAgent || !OwnsTerminal(Caller()->pid) || accountStore.Status() != AccountStore::Empty)
    {
        return HACKOS_DENIED;
    }

    char name[HACKOS_ACCOUNT_NAME];
    if (!Caller()->userSpace->CopyString(name, (uint64_t)username, sizeof(name)))
    {
        return HACKOS_ERROR;
    }

    PasswordRecord record = {};
    uint64_t result = NewPassword(password, record);
    if (result)
    {
        return result;
    }

    AccountRegistry previous = accounts;
    bool prepared = accounts.Setup(name, record);
    SecureErase(&record, sizeof(record));
    return prepared ? SaveAccounts(0, previous) : HACKOS_ERROR;
}

static uint64_t UserPasswordSet(uint64_t uid, const char* password)
{
    if (!IsRoot())
    {
        return HACKOS_DENIED;
    }

    if (accountStore.Status() != AccountStore::Ready || !accounts.Credentials(uid))
    {
        return HACKOS_ERROR;
    }

    PasswordRecord record = {};
    uint64_t result = NewPassword(password, record);
    if (result)
    {
        return result;
    }

    AccountRegistry previous = accounts;
    accounts.SetPassword(uid, record);
    SecureErase(&record, sizeof(record));
    return SaveAccounts(0, previous);
}

static uint64_t UserPasswordChange(const char* oldPassword, const char* newPassword)
{
    static uint64_t retryAfter = 0;
    if (accountStore.Status() != AccountStore::Ready || tick < retryAfter)
    {
        return HACKOS_RETRY;
    }
    const PasswordRecord* stored = nullptr;
    for (unsigned i = 0; i < accounts.Data().userCount; ++i)
    {
        if (accounts.User(i)->credentials.uid == Caller()->credentials.uid)
        {
            stored = &accounts.Data().passwords[i];
            break;
        }
    }
    char password[HACKOS_PASSWORD_MAX + 1] = {};
    if (!stored || !stored->enabled || !CopyPassword(oldPassword, password))
    {
        SecureErase(password, sizeof(password));
        retryAfter = tick + TimerTicksForMilliseconds(1000);
        return HACKOS_DENIED;
    }
    uint8_t digest[32];
    Pbkdf2Sha256(password, strlen(password), stored->salt, sizeof(stored->salt), stored->iterations, digest);
    bool accepted = ConstantEqual(digest, stored->digest, sizeof(digest));
    SecureErase(password, sizeof(password));
    SecureErase(digest, sizeof(digest));
    if (!accepted)
    {
        retryAfter = tick + TimerTicksForMilliseconds(1000);
        return HACKOS_DENIED;
    }
    PasswordRecord record = {};
    auto result = NewPassword(newPassword, record);
    if (result)
    {
        SecureErase(&record, sizeof(record));
        return result;
    }
    AccountRegistry previous = accounts;
    accounts.SetPassword(Caller()->credentials.uid, record);
    SecureErase(&record, sizeof(record));
    result = SaveAccounts(0, previous);
    SecureErase(&previous, sizeof(previous));
    return result;
}

static uint64_t Authenticate(const char* username, const char* source, bool desktop)
{
    static uint64_t retryAfter = 0;
    if (!Caller()->authenticationAgent || !OwnsTerminal(Caller()->pid) || accountStore.Status() != AccountStore::Ready)
    {
        return HACKOS_DENIED;
    }

    if (tick < retryAfter)
    {
        return HACKOS_RETRY;
    }

    char name[HACKOS_ACCOUNT_NAME], password[HACKOS_PASSWORD_MAX + 1] = {};
    if (!Caller()->userSpace->CopyString(name, (uint64_t)username, sizeof(name)) || !CopyPassword(source, password))
    {
        SecureErase(password, sizeof(password));
        retryAfter = tick + TimerTicksForMilliseconds(1000);
        return HACKOS_DENIED;
    }

    int index = accounts.FindUser(name);
    PasswordRecord record = {};
    if (index >= 0)
    {
        record = accounts.Data().passwords[index];
    }

    uint8_t digest[32];
    Pbkdf2Sha256(password, strlen(password), record.salt, sizeof(record.salt), PASSWORD_ITERATIONS, digest);
    bool accepted = record.enabled && ConstantEqual(digest, record.digest, 32);
    SecureErase(password, sizeof(password));
    SecureErase(&record, sizeof(record));
    SecureErase(digest, sizeof(digest));
    if (!accepted)
    {
        retryAfter = tick + TimerTicksForMilliseconds(1000);
        return HACKOS_DENIED;
    }

    const auto& identity = accounts.User(index)->credentials;
    uint64_t pid = LoadUserProgramTask(desktop ? "~/USR/BIN/DESKTOP" : "~/USR/BIN/SHELL", true, 0, nullptr, &identity);
    for (auto p = ready_queue; p; p = p->next)
    {
        if (p->pid == pid)
        {
            p->sessionId = pid;
            p->desktopController = desktop;
            p->terminalReturnPid = Caller()->pid;
        }
    }

    return pid ? pid : HACKOS_ERROR;
}

static uint64_t UserAuthLogin(const char* username, const char* password)
{
    return Authenticate(username, password, false);
}

static uint64_t UserAuthDesktop(const char* username, const char* password)
{
    return Authenticate(username, password, true);
}

static bool MutationPath(const char* name, char (&path)[256]);

static uint64_t OpenUserFile(const char* name, uint64_t mode, ActiveFile*& destination)
{
    char path[256];
    if (!Path(name, path) || !(mode & 3) || (mode & ~7ULL) ||
        ((mode & HACKOS_OPEN_CREATE) && !(mode & HACKOS_OPEN_WRITE)))
    {
        return HACKOS_ERROR;
    }

    bool write = mode & HACKOS_OPEN_WRITE;
    if (write && !MutationPath(name, path))
    {
        return HACKOS_ERROR;
    }

    if (StreamPathBusy(path, true))
    {
        return HACKOS_ERROR;
    }

    // Exclusive writers; readers share an immutable snapshot until all close.
    for (auto process = ready_queue; process; process = process->next)
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            auto file = process->files[i];
            if (file && !strcmp((char*)file->GetPath(), path) && (file->accessMode & HACKOS_OPEN_WRITE))
            {
                return HACKOS_ERROR;
            }
        }
    }

    ActiveFile* file = nullptr;
    int status = filesystems[0]->Lookup(path, file);
    if (status == -2 && (mode & HACKOS_OPEN_CREATE))
    {
        if (!PathAccess(path, Caller()->credentials, 3, true))
        {
            return HACKOS_DENIED;
        }

        FilePermissions permissions = {Caller()->credentials.uid, Caller()->credentials.gid, 0644};
        if (filesystems[0]->SaveFile(path, nullptr, 0, true, &permissions))
        {
            return HACKOS_ERROR;
        }

        status = filesystems[0]->Lookup(path, file);
    }

    if (status || !file)
    {
        return HACKOS_ERROR;
    }

    unsigned access = (mode & HACKOS_OPEN_READ ? 4 : 0) | (write ? 2 : 0);
    if ((file->GetAttributes() & DIRECTORY) || (write && (file->GetAttributes() & READ_ONLY)) ||
        !PathAccess(path, Caller()->credentials, access))
    {
        filesystems[0]->CloseFile(file);
        return HACKOS_ERROR;
    }

    file->accessMode = mode;
    destination = file;
    ++user_files_live;
    return 0;
}

static uint64_t UserOpenMode(const char* name, uint64_t mode)
{
    if (nextHandle == ~0ULL - 5)
    {
        return HACKOS_ERROR;
    }

    for (unsigned i = 0; i < 8; ++i)
    {
        if (!Caller()->files[i])
        {
            uint64_t result = OpenUserFile(name, mode, Caller()->files[i]);
            return result ? result : Caller()->fileHandles[i] = ++nextHandle;
        }
    }

    return HACKOS_ERROR;
}

static uint64_t UserOpen(const char* name)
{
    return UserOpenMode(name, HACKOS_OPEN_READ);
}

static int Handle(uint64_t handle)
{
    for (int i = 0; i < 8; ++i)
    {
        if (Caller()->files[i] && Caller()->fileHandles[i] == handle)
        {
            return i;
        }
    }

    return -1;
}

static uint64_t UserRead(uint64_t handle, void* destination, uint64_t length)
{
    int slot = Handle(handle);
    if (slot < 0 || length > 512 || !Caller()->userSpace->WritableRange((uint64_t)destination, length))
    {
        return HACKOS_ERROR;
    }

    auto file = Caller()->files[slot];
    if (!(file->accessMode & HACKOS_OPEN_READ))
    {
        return HACKOS_DENIED;
    }

    uint64_t size = file->GetSize();
    uint64_t remaining = file->seek_position < size ? size - file->seek_position : 0;
    if (length > remaining)
    {
        length = remaining;
    }

    char buffer[512] = {};
    if (length && filesystems[0]->Read(file, buffer, length) != 0)
    {
        return HACKOS_ERROR;
    }

    return Output(destination, buffer, length) ? length : HACKOS_ERROR;
}

static uint64_t UserClose(uint64_t handle)
{
    int slot = Handle(handle);
    if (slot < 0)
    {
        return HACKOS_ERROR;
    }

    int status = Caller()->files[slot]->accessMode & HACKOS_OPEN_WRITE ? filesystems[0]->Flush() : 0;
    filesystems[0]->CloseFile(Caller()->files[slot]);
    --user_files_live;
    Caller()->files[slot] = nullptr;
    Caller()->fileHandles[slot] = 0;
    return status ? HACKOS_ERROR : 0;
}

static uint64_t UserFdOpen(const char* name, uint64_t mode)
{
    bool truncate = mode & HACKOS_OPEN_TRUNCATE;
    bool append = mode & HACKOS_OPEN_APPEND;
    int slot = FreeDescriptor(Caller());
    if (slot < 0 || (mode & ~31ULL) || ((truncate || append) && !(mode & HACKOS_OPEN_WRITE)) || (truncate && append))
    {
        return HACKOS_ERROR;
    }

    auto stream = AllocateStream();
    if (!stream)
    {
        return HACKOS_ERROR;
    }

    // Reserve all bookkeeping before opening/creating/truncating any file.
    Caller()->descriptors[slot] = stream;
    uint64_t result = OpenUserFile(name, mode & 7, stream->file);
    if (result)
    {
        UserFdClose(slot);
        return result;
    }

    auto file = stream->file;
    stream->append = append;
    if (truncate)
    {
        char path[256];
        Name(path, file->GetPath());
        filesystems[0]->CloseFile(file);
        stream->file = nullptr;
        --user_files_live;
        if (filesystems[0]->SaveFile(path, nullptr, 0, false) || filesystems[0]->Lookup(path, file))
        {
            UserFdClose(slot);
            return HACKOS_ERROR;
        }

        file->accessMode = mode & 7;
        stream->file = file;
        ++user_files_live;
    }

    return slot;
}

static uint64_t UserSpawnIo(const char* name, uint64_t argc, const char* const* argv, const HackosSpawnIo* source)
{
    HackosSpawnIo specification;
    if (!Caller()->userSpace->CopyFromUser(&specification, (uint64_t)source, sizeof(specification)))
    {
        return HACKOS_ERROR;
    }

    for (auto fd : specification.streams)
    {
        if (fd != HACKOS_ERROR && (fd >= HACKOS_FD_LIMIT || !Caller()->descriptors[fd]))
        {
            return HACKOS_ERROR;
        }
    }

    ChildResult* leader = nullptr;
    if (specification.group)
    {
        for (auto& child : Caller()->children)
        {
            if (child.pid == specification.group && child.job)
            {
                leader = &child;
            }
        }

        bool held = false;
        for (auto process = ready_queue; process; process = process->next)
        {
            if (process->processGroup == specification.group && process->jobOwnerPid == Caller()->pid &&
                process->state == PROCESS_STATE_STARTING)
            {
                held = true;
            }
        }

        if (!leader || !held)
        {
            return HACKOS_DENIED;
        }
    }

    uint64_t pid = UserSpawnArgs(name, argc, argv);
    if (pid == HACKOS_ERROR || pid == HACKOS_DENIED)
    {
        return pid;
    }

    uint64_t group = specification.group ? specification.group : pid;
    for (auto process = ready_queue; process; process = process->next)
    {
        if (process->pid == pid)
        {
            SelectStreams(process, Caller(), specification);
            process->pipelineMember = true;
            process->processGroup = group;
            process->jobOwnerPid = Caller()->pid;
            process->state = PROCESS_STATE_STARTING;
        }
    }

    for (auto& child : Caller()->children)
    {
        if (child.pid == pid)
        {
            child.group = group;
            child.job = pid == group;
            if (!leader)
            {
                leader = &child;
            }
        }
    }

    leader->statusPid = pid;
    return pid;
}

static uint64_t UserStat(const char* name, HackosFileInfo* destination)
{
    char path[256];
    if (!Path(name, path) || !Caller()->userSpace->WritableRange((uint64_t)destination, sizeof(*destination)))
    {
        return HACKOS_ERROR;
    }

    if (!PathAccess(path, Caller()->credentials, 1, true))
    {
        return HACKOS_DENIED;
    }

    HackosFileInfo info = {};
    ActiveFile* file = nullptr;
    int status = filesystems[0]->Lookup(path, file);
    if (status)
    {
        return status == -2 ? HACKOS_NOT_FOUND : HACKOS_ERROR;
    }

    Info(info, file);
    filesystems[0]->CloseFile(file);

    return Output(destination, &info, sizeof(info)) ? 0 : HACKOS_ERROR;
}

static uint64_t UserReadDirBatch(const char* name, uint64_t index, HackosFileInfo* destination, uint64_t capacity)
{
    char path[256];
    if (!capacity || capacity > HACKOS_READDIR_BATCH_MAX || !Path(name, path) ||
        !Caller()->userSpace->WritableRange((uint64_t)destination, capacity * sizeof(*destination)))
    {
        return HACKOS_ERROR;
    }
    if (path[1])
    {
        auto directory = filesystems[0]->OpenFile(path);
        if (!directory)
        {
            return HACKOS_ERROR;
        }
        bool isDirectory = directory->GetAttributes() & DIRECTORY;
        filesystems[0]->CloseFile(directory);
        if (!isDirectory)
        {
            return HACKOS_ERROR;
        }
    }
    if (!PathAccess(path, Caller()->credentials, 5))
    {
        return HACKOS_DENIED;
    }
    HackosFileInfo entries[HACKOS_READDIR_BATCH_MAX];
    uint64_t count = 0;
    if (filesystems[0]->ReadDirectory(path, index, entries, capacity, count))
    {
        return HACKOS_ERROR;
    }
    if (!count)
    {
        return 0;
    }
    return Output(destination, entries, count * sizeof(*entries)) ? count : HACKOS_ERROR;
}

static uint64_t UserReadDir(const char* name, uint64_t index, HackosFileInfo* destination)
{
    return UserReadDirBatch(name, index, destination, 1);
}

static uint64_t UserGetCwd(char* destination, uint64_t capacity)
{
    uint64_t length = 0;
    while (Caller()->working_dir[length])
    {
        length++;
    }

    if (capacity <= length)
    {
        return HACKOS_ERROR;
    }

    return Output(destination, Caller()->working_dir, length + 1) ? 0 : HACKOS_ERROR;
}

static uint64_t UserChdir(const char* name)
{
    char path[256];
    if (!Path(name, path))
    {
        return HACKOS_ERROR;
    }

    if (path[1])
    {
        auto file = filesystems[0]->OpenFile(path);
        if (!file)
        {
            return HACKOS_ERROR;
        }

        bool directory = file->GetAttributes() & DIRECTORY;
        filesystems[0]->CloseFile(file);
        if (!directory)
        {
            return HACKOS_ERROR;
        }
    }

    if (!PathAccess(path, Caller()->credentials, 1))
    {
        return HACKOS_DENIED;
    }

    Name(Caller()->working_dir, path);
    return 0;
}

static bool MutationPath(const char* name, char (&path)[256])
{
    if (!Path(name, path) || !path[1] || Caller()->authenticationAgent || strcmp(path, (char*)"~/SYSTEM") == 0 ||
        strncmp(path, (char*)"~/SYSTEM/", 9) == 0)
    {
        return false;
    }

    if (StreamPathBusy(path))
    {
        return false;
    }

    // Read handles contain a metadata snapshot. Reject mutation while any
    // process holds the path, including unreaped zombies, instead of making
    // their cluster/size records stale. Handles are never transferred.
    for (auto process = ready_queue; process; process = process->next)
    {
        for (unsigned i = 0; i < 8; ++i)
        {
            if (process->files[i] && strcmp((char*)process->files[i]->GetPath(), path) == 0)
            {
                return false;
            }
        }
    }

    return true;
}

static uint64_t UserFileSave(const char* name, const void* data, uint64_t size, uint64_t flags)
{
    char path[256];
    if (size > HACKOS_FILE_LIMIT || (flags & ~HACKOS_SAVE_CREATE) || !MutationPath(name, path))
    {
        return HACKOS_ERROR;
    }

    if (!PathAccess(path, Caller()->credentials, flags & HACKOS_SAVE_CREATE ? 3 : 2, flags & HACKOS_SAVE_CREATE))
    {
        return HACKOS_DENIED;
    }

    // Copy the entire payload before any storage mutation. No userspace heap
    // or writable handle is needed for this bounded whole-file operation.
    void* copy = (void*)kmalloc(size ? size : 1);
    if (!copy)
    {
        return HACKOS_ERROR;
    }

    bool copied = Caller()->userSpace->CopyFromUser(copy, (uint64_t)data, size);
    FilePermissions permissions = {Caller()->credentials.uid, Caller()->credentials.gid, 0644};
    int status = copied ? filesystems[0]->SaveFile(path, copy, size, flags & HACKOS_SAVE_CREATE, &permissions) : -1;
    kfree(copy);
    return status ? HACKOS_ERROR : 0;
}

static uint64_t UserMkdir(const char* name)
{
    char path[256];
    if (!MutationPath(name, path))
    {
        return HACKOS_ERROR;
    }
    if (!PathAccess(path, Caller()->credentials, 3, true))
    {
        return HACKOS_DENIED;
    }
    FilePermissions permissions = {Caller()->credentials.uid, Caller()->credentials.gid, 0755};
    return filesystems[0]->MakeDirectory(path, &permissions) ? HACKOS_ERROR : 0;
}

static uint64_t UserUnlink(const char* name)
{
    char path[256];
    if (!MutationPath(name, path))
    {
        return HACKOS_ERROR;
    }
    if (!PathAccess(path, Caller()->credentials, 3, true))
    {
        return HACKOS_DENIED;
    }
    return filesystems[0]->RemoveFile(path) ? HACKOS_ERROR : 0;
}

static uint64_t UserRename(const char* source, const char* destination)
{
    char from[256], to[256];
    if (!MutationPath(source, from) || !MutationPath(destination, to))
    {
        return HACKOS_ERROR;
    }
    if (!PathAccess(from, Caller()->credentials, 3, true) || !PathAccess(to, Caller()->credentials, 3, true))
    {
        return HACKOS_DENIED;
    }
    return filesystems[0]->MoveFile(from, to) ? HACKOS_ERROR : 0;
}

static uint64_t UserFileWrite(uint64_t handle, const void* source, uint64_t length)
{
    int slot = Handle(handle);
    if (slot < 0 || length > 512)
    {
        return HACKOS_ERROR;
    }

    auto file = Caller()->files[slot];
    if (!(file->accessMode & HACKOS_OPEN_WRITE))
    {
        return HACKOS_DENIED;
    }

    if (file->seek_position > HACKOS_FILE_LIMIT || length > HACKOS_FILE_LIMIT - file->seek_position)
    {
        return HACKOS_ERROR;
    }

    uint8_t data[512];
    if (!Caller()->userSpace->CopyFromUser(data, (uint64_t)source, length))
    {
        return HACKOS_ERROR;
    }

    return filesystems[0]->Write(file, data, length) ? HACKOS_ERROR : length;
}

static uint64_t UserSeek(uint64_t handle, uint64_t offset)
{
    int slot = Handle(handle);
    if (slot < 0 || offset > HACKOS_FILE_LIMIT)
    {
        return HACKOS_ERROR;
    }

    Caller()->files[slot]->seek_position = offset;
    return offset;
}

static uint64_t UserFlush(uint64_t handle)
{
    return Handle(handle) >= 0 && !filesystems[0]->Flush() ? 0 : HACKOS_ERROR;
}

static uint64_t ChangePermissions(const char* name, uint64_t mode, uint64_t uid, uint64_t gid, bool owner)
{
    char path[256];
    if (!MutationPath(name, path) || !PathAccess(path, Caller()->credentials, 1, true) || mode > 0777 ||
        uid > 0xffffffff || gid > 0xffffffff)
    {
        return HACKOS_ERROR;
    }

    ActiveFile* file = nullptr;
    if (filesystems[0]->Lookup(path, file))
    {
        return HACKOS_ERROR;
    }

    auto permissions = file->GetPermissions();
    filesystems[0]->CloseFile(file);
    if (!IsRoot() && (owner || Caller()->credentials.uid != permissions.uid))
    {
        return HACKOS_DENIED;
    }

    if (owner)
    {
        bool validGroup = false;
        for (uint64_t i = 0; accounts.Group(i); ++i)
        {
            validGroup = validGroup || accounts.Group(i)->gid == gid;
        }

        if (!accounts.Credentials(uid) || !validGroup)
        {
            return HACKOS_ERROR;
        }

        permissions.uid = uid;
        permissions.gid = gid;
    }
    else
    {
        permissions.mode = mode;
    }

    return filesystems[0]->SetPermissions(path, permissions) ? HACKOS_ERROR : 0;
}

static uint64_t UserChmod(const char* name, uint64_t mode)
{
    return ChangePermissions(name, mode, 0, 0, false);
}

static uint64_t UserChown(const char* name, uint64_t uid, uint64_t gid)
{
    return ChangePermissions(name, 0, uid, gid, true);
}

static uint64_t UserPower(uint64_t operation)
{
    // Only the boot login agent may offer physical-console power controls
    // without root credentials, and only while it owns the logged-out terminal.
    if (!IsRoot() && !(Caller()->authenticationAgent && OwnsTerminal(Caller()->pid)))
    {
        return HACKOS_DENIED;
    }

    if (operation == 0)
    {
        auto fadt = kInfo->rsdp ? (ACPI::FADT*)((ACPI::RSDP*)kInfo->rsdp)->GetSystemTable("FACP") : nullptr;
        if (!fadt)
        {
            return HACKOS_ERROR;
        }

        kprintf("Stopping the CPU. Bye!\n");
        Display::SharedDisplay()->DrawBackbuffer();
        return fadt->Shutdown() ? 0 : HACKOS_ERROR;
    }
    else if (operation == 1)
    {
        kprintf("Restarting the CPU. Bye!\n");
        __outb(0x92, __inb(0x92) | 1);
        return 0;
    }

    return HACKOS_ERROR; // Unknown operation.
}

static void syscall_handler(Registers* r)
{
    // There is no ring-0 syscall ABI. Kernel code calls internal functions.
    if ((r->cs & 3) != 3 || !Caller() || !Caller()->userSpace)
    {
        r->rax = HACKOS_ERROR;
        return;
    }

    if (r->rax == HACKOS_SYS_exit)
    {
        ExitUserProcess(r->rdi);
    }

    if (r->rax == HACKOS_SYS_yield)
    {
        ScheduleIRQ();
        r->rax = 0;
        return;
    }

    if (r->rax == HACKOS_SYS_wait)
    {
        r->rax = WaitForChild(r->rdi, (int64_t*)r->rsi, r->rdx);
        return;
    }

    if (r->rax == HACKOS_SYS_terminal_read)
    {
        r->rax = ReadTerminal(r->rdi);
        return;
    }

    if (r->rax == HACKOS_SYS_job_wait)
    {
        r->rax = WaitForJob(r->rdi, (int64_t*)r->rsi, r->rdx);
        return;
    }

    // These calls can suspend the current kernel stack. Never put a
    // PreemptionGuard around them; each predicate/state change is IRQ-atomic.
    switch (r->rax)
    {
    case HACKOS_SYS_user_write:
        r->rax = user_write((const char*)r->rdi, r->rsi);
        return;
    case HACKOS_SYS_fd_read:
        r->rax = UserFdRead(r->rdi, (void*)r->rsi, r->rdx, r->rcx);
        return;
    case HACKOS_SYS_fd_write:
        r->rax = UserFdWrite(r->rdi, (const void*)r->rsi, r->rdx, r->rcx);
        return;
    case HACKOS_SYS_poll:
        r->rax = UserPoll((HackosPollFd*)r->rdi, r->rsi, r->rdx);
        return;
    case HACKOS_SYS_wait_timeout:
        r->rax = WaitForChildTimeout(r->rdi, (int64_t*)r->rsi, r->rdx, r->rcx);
        return;
    case HACKOS_SYS_job_wait_timeout:
        r->rax = WaitForJobTimeout(r->rdi, (int64_t*)r->rsi, r->rdx, r->rcx);
        return;
    case HACKOS_SYS_session_logout:
        r->rax = UserSessionLogout();
        return;
    case HACKOS_SYS_sleep:
        r->rax = UserSleep(r->rdi);
        return;
    }

    PreemptionGuard guard;
    switch (r->rax)
    {
    case HACKOS_SYS_auth_desktop:
        r->rax = UserAuthDesktop((const char*)r->rdi, (const char*)r->rsi);
        break;
    case HACKOS_SYS_desktop:
        r->rax = UserDesktop(r->rdi, r->rsi, (HackosDesktopEntry*)r->rdx);
        break;
    case HACKOS_SYS_terminal_session:
        r->rax = UserTerminalSession(r->rdi, r->rsi, (void*)r->rdx);
        break;
    case HACKOS_SYS_fd_open:
        r->rax = UserFdOpen((const char*)r->rdi, r->rsi);
        break;
    case HACKOS_SYS_fd_close:
        r->rax = UserFdClose(r->rdi);
        break;
    case HACKOS_SYS_dup2:
        r->rax = UserDup2(r->rdi, r->rsi);
        break;
    case HACKOS_SYS_pipe:
        r->rax = UserPipe((uint64_t*)r->rdi);
        break;
    case HACKOS_SYS_fd_seek:
        r->rax = UserFdSeek(r->rdi, r->rsi);
        break;
    case HACKOS_SYS_fd_flush:
        r->rax = UserFdFlush(r->rdi);
        break;
    case HACKOS_SYS_terminal_info:
        r->rax = UserTerminalInfo(r->rdi, (HackosTerminalInfo*)r->rsi);
        break;
    case HACKOS_SYS_terminal_cursor:
        r->rax = UserTerminalCursor(r->rdi, r->rsi);
        break;
    case HACKOS_SYS_readdir_batch:
        r->rax = UserReadDirBatch((const char*)r->rdi, r->rsi, (HackosFileInfo*)r->rdx, r->rcx);
        break;
    case HACKOS_SYS_terminal_get:
        r->rax = UserTerminalGet((HackosTerminalSettings*)r->rdi);
        break;
    case HACKOS_SYS_terminal_set:
        r->rax = UserTerminalSet(r->rdi, r->rsi);
        break;
    case HACKOS_SYS_spawn_io:
        r->rax = UserSpawnIo((const char*)r->rdi, r->rsi, (const char* const*)r->rdx, (const HackosSpawnIo*)r->rcx);
        break;
    case HACKOS_SYS_spawn_job:
        r->rax = UserSpawnJob((const char*)r->rdi, r->rsi, (const char* const*)r->rdx, r->rcx);
        break;
    case HACKOS_SYS_job_control:
        r->rax = ControlJob(r->rdi, r->rsi);
        break;
    case HACKOS_SYS_heap_resize:
        r->rax = Caller()->userSpace->ResizeHeap(r->rdi) ? HACKOS_HEAP_BASE : HACKOS_ERROR;
        Caller()->pageTable->SetAsCurrent();
        break;
    case HACKOS_SYS_input:
        r->rax = UserInput();
        break;
    case HACKOS_SYS_console:
        r->rax = UserConsole(r->rdi);
        break;
    case HACKOS_SYS_clock:
        r->rax = UserClock((HackosClock*)r->rdi);
        break;
    case HACKOS_SYS_echo:
    {
        HackosEcho record = {};
        if (r->rdi == HACKOS_ECHO_START &&
            !Caller()->userSpace->CopyFromUser(&record, r->rsi, sizeof(record)))
        {
            r->rax = HACKOS_ERROR;
            break;
        }
        r->rax = Net::EchoControl(Caller()->pid, r->rdi, record);
        if (!r->rax && r->rdi == HACKOS_ECHO_QUERY && !Output((void*)r->rsi, &record, sizeof(record)))
        {
            r->rax = HACKOS_ERROR;
        }
        break;
    }
    case HACKOS_SYS_resolve:
    {
        HackosResolve record = {};
        if (r->rdi == HACKOS_RESOLVE_START &&
            !Caller()->userSpace->CopyFromUser(&record, r->rsi, sizeof(record)))
        {
            r->rax = HACKOS_ERROR;
            break;
        }
        r->rax = Net::ResolveControl(Caller()->pid, r->rdi, record);
        if (!r->rax && r->rdi == HACKOS_RESOLVE_QUERY && !Output((void*)r->rsi, &record, sizeof(record)))
        {
            r->rax = HACKOS_ERROR;
        }
        break;
    }
    case HACKOS_SYS_tcp_stream:
    {
        HackosTcpStream record = {};
        if ((r->rdi == HACKOS_TCP_CONNECT || r->rdi == HACKOS_TCP_WRITE || r->rdi == HACKOS_TCP_READ) &&
            !Caller()->userSpace->CopyFromUser(&record, r->rdx, sizeof(record)))
        {
            r->rax = HACKOS_ERROR;
            break;
        }
        r->rax = Net::TcpStreamControl(Caller()->pid, r->rdi, r->rsi, record);
        if (!r->rax && (r->rdi == HACKOS_TCP_READ || r->rdi == HACKOS_TCP_STATUS))
        {
            if (!Output((void*)r->rdx, &record, sizeof(record)))
            {
                r->rax = HACKOS_ERROR;
            }
            else if (r->rdi == HACKOS_TCP_READ)
            {
                Net::ConsumeTcp(Caller()->pid, r->rsi, record.size);
            }
        }
        break;
    }
    case HACKOS_SYS_tcp:
    {
        HackosTcp record = {};
        if (r->rdi == HACKOS_TCP_START &&
            !Caller()->userSpace->CopyFromUser(&record, r->rsi, sizeof(record)))
        {
            r->rax = HACKOS_ERROR;
            break;
        }
        r->rax = Net::TcpControl(Caller()->pid, r->rdi, record);
        if (!r->rax && r->rdi == HACKOS_TCP_QUERY && !Output((void*)r->rsi, &record, sizeof(record)))
        {
            r->rax = HACKOS_ERROR;
        }
        break;
    }
    case HACKOS_SYS_udp:
    {
        HackosDatagram record = {};
        if ((r->rdi == HACKOS_UDP_OPEN || r->rdi == HACKOS_UDP_SEND) &&
            !Caller()->userSpace->CopyFromUser(&record, r->rdx, sizeof(record)))
        {
            r->rax = HACKOS_ERROR;
            break;
        }
        r->rax = Net::UdpControl(Caller()->pid, r->rdi, r->rsi, record);
        if (!r->rax && (r->rdi == HACKOS_UDP_RECEIVE || r->rdi == HACKOS_UDP_STATUS))
        {
            if (!Output((void*)r->rdx, &record, sizeof(record)))
            {
                r->rax = HACKOS_ERROR;
            }
            else if (r->rdi == HACKOS_UDP_RECEIVE)
            {
                Net::ConsumeUdp(Caller()->pid, r->rsi);
            }
        }
        break;
    }
    case HACKOS_SYS_hardware:
    {
        if (!IsRoot())
        {
            r->rax = HACKOS_DENIED;
            break;
        }

        HackosHardwareInfo info = {};
        r->rax = ReadHardware(r->rdi, r->rsi, info);
        if (r->rax == 1 && !Output((void*)r->rdx, &info, sizeof(info)))
        {
            r->rax = HACKOS_ERROR;
        }
        break;
    }
    case HACKOS_SYS_process_info:
        r->rax = UserProcessInfo(r->rdi, (HackosProcessInfo*)r->rsi);
        break;
    case HACKOS_SYS_kill:
        r->rax = UserKill(r->rdi);
        break;
    case HACKOS_SYS_spawn:
        r->rax = UserSpawn((const char*)r->rdi);
        break;
    case HACKOS_SYS_spawn_args:
        r->rax = UserSpawnArgs((const char*)r->rdi, r->rsi, (const char* const*)r->rdx);
        break;
    case HACKOS_SYS_spawn_foreground:
        r->rax = UserSpawnForeground((const char*)r->rdi, r->rsi, (const char* const*)r->rdx);
        break;
    case HACKOS_SYS_open_mode:
        r->rax = UserOpenMode((const char*)r->rdi, r->rsi);
        break;
    case HACKOS_SYS_file_write:
        r->rax = UserFileWrite(r->rdi, (const void*)r->rsi, r->rdx);
        break;
    case HACKOS_SYS_seek:
        r->rax = UserSeek(r->rdi, r->rsi);
        break;
    case HACKOS_SYS_flush:
        r->rax = UserFlush(r->rdi);
        break;
    case HACKOS_SYS_chmod:
        r->rax = UserChmod((const char*)r->rdi, r->rsi);
        break;
    case HACKOS_SYS_chown:
        r->rax = UserChown((const char*)r->rdi, r->rsi, r->rdx);
        break;
    case HACKOS_SYS_file_save:
        r->rax = UserFileSave((const char*)r->rdi, (const void*)r->rsi, r->rdx, r->rcx);
        break;
    case HACKOS_SYS_mkdir:
        r->rax = UserMkdir((const char*)r->rdi);
        break;
    case HACKOS_SYS_unlink:
        r->rax = UserUnlink((const char*)r->rdi);
        break;
    case HACKOS_SYS_rename:
        r->rax = UserRename((const char*)r->rdi, (const char*)r->rsi);
        break;
    case HACKOS_SYS_open:
        r->rax = UserOpen((const char*)r->rdi);
        break;
    case HACKOS_SYS_read:
        r->rax = UserRead(r->rdi, (void*)r->rsi, r->rdx);
        break;
    case HACKOS_SYS_close:
        r->rax = UserClose(r->rdi);
        break;
    case HACKOS_SYS_stat:
        r->rax = UserStat((const char*)r->rdi, (HackosFileInfo*)r->rsi);
        break;
    case HACKOS_SYS_readdir:
        r->rax = UserReadDir((const char*)r->rdi, r->rsi, (HackosFileInfo*)r->rdx);
        break;
    case HACKOS_SYS_power:
        r->rax = UserPower(r->rdi);
        break;
    case HACKOS_SYS_getcwd:
        r->rax = UserGetCwd((char*)r->rdi, r->rsi);
        break;
    case HACKOS_SYS_chdir:
        r->rax = UserChdir((const char*)r->rdi);
        break;
    case HACKOS_SYS_auth_state:
        r->rax = UserAuthState();
        break;
    case HACKOS_SYS_auth_setup:
        r->rax = UserAuthSetup((const char*)r->rdi, (const char*)r->rsi);
        break;
    case HACKOS_SYS_auth_login:
        r->rax = UserAuthLogin((const char*)r->rdi, (const char*)r->rsi);
        break;
    case HACKOS_SYS_account_edit:
        r->rax = UserAccountEdit(r->rdi, r->rsi, r->rdx, (const char*)r->rcx);
        break;
    case HACKOS_SYS_password_change:
        r->rax = UserPasswordChange((const char*)r->rdi, (const char*)r->rsi);
        break;
    case HACKOS_SYS_password_set:
        r->rax = UserPasswordSet(r->rdi, (const char*)r->rsi);
        break;
    case HACKOS_SYS_identity:
        r->rax = UserIdentity((HackosCredentials*)r->rdi);
        break;
    case HACKOS_SYS_user_info:
        r->rax = UserAccountInfo(r->rdi, (HackosUserInfo*)r->rsi);
        break;
    case HACKOS_SYS_group_info:
        r->rax = UserGroupInfo(r->rdi, (HackosGroupInfo*)r->rsi);
        break;
    case HACKOS_SYS_user_create:
        r->rax = UserCreateAccount((const char*)r->rdi, r->rsi);
        break;
    case HACKOS_SYS_group_create:
        r->rax = UserCreateGroup((const char*)r->rdi);
        break;
    case HACKOS_SYS_user_add_group:
        r->rax = UserAddGroup(r->rdi, r->rsi);
        break;
    case HACKOS_SYS_spawn_as:
        r->rax = UserSpawnAs((const char*)r->rdi, r->rsi, (const char* const*)r->rdx, r->rcx, r->r8);
        break;
    case HACKOS_SYS_window_input:
        r->rax = UserWindowInput(r->rdi, r->rsi, (HackosWindowInputState*)r->rdx);
        break;
    case HACKOS_SYS_window_event2:
        r->rax = UserWindowEvent2(r->rdi, (HackosWindowEvent2*)r->rsi);
        break;
    case HACKOS_SYS_window_close_reply:
        r->rax = UserWindowCloseReply(r->rdi, r->rsi);
        break;
    case HACKOS_SYS_clipboard:
        r->rax = UserClipboard(r->rdi, (void*)r->rsi, r->rdx);
        break;
    case HACKOS_SYS_window_frame:
        r->rax = UserWindowFrame(r->rdi, r->rsi, (HackosWindowFrame*)r->rdx);
        break;
    case HACKOS_SYS_window_upload:
        r->rax = UserWindowUpload(r->rdi, (const HackosWindowUpload*)r->rsi, (const uint32_t*)r->rdx);
        break;
    case HACKOS_SYS_window_mode:
        r->rax = UserWindowMode(r->rdi, r->rsi);
        break;
    case HACKOS_SYS_window_state:
        r->rax = UserWindowState(r->rdi, (HackosWindowState*)r->rsi);
        break;
    case HACKOS_SYS_window_event:
        r->rax = UserWindowEvent(r->rdi, (HackosWindowEvent*)r->rsi);
        break;
    case HACKOS_SYS_window_create:
        r->rax = UserWindowCreate((const HackosWindowSpec*)r->rdi);
        break;
    case HACKOS_SYS_window_blit:
        r->rax = UserWindowBlit(r->rdi, (const HackosWindowRect*)r->rsi, (const uint32_t*)r->rdx);
        break;
    case HACKOS_SYS_window_poll:
        r->rax = UserWindowPoll(r->rdi);
        break;
    case HACKOS_SYS_window_close:
        r->rax = UserWindowClose(r->rdi);
        break;
    default:
        r->rax = HACKOS_ERROR;
        break;
    }
}

void InitialiseSyscalls()
{
    accounts.Initialise();
    InitialiseAccountStorage();
    RegisterInterruptHandler(0x80, syscall_handler);
}
