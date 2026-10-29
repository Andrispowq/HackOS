#include "terminals.h"
#include "user.h"
#include "address_space.h"
#include "proc/tasking/process.h"
#include "drivers/screen/screen.h"
#include "drivers/screen/window_manager.h"
#include "drivers/serial/serial.h"
#include "arch/x86_64/interrupts/guard.h"
#include "lib/memory.h"
#include "lib/string.h"

extern Process* ready_queue;
extern uint64_t ConsoleTerminalOwner();
TerminalSession terminals[HACKOS_TTY_LIMIT];

TerminalSession* ProcessTerminal(const Process* process)
{
    if (!process || !process->terminalId || process->terminalId > HACKOS_TTY_LIMIT)
    {
        return nullptr;
    }
    auto tty = &terminals[process->terminalId - 1];
    return tty->controller ? tty : nullptr;
}

uint64_t TerminalOwner(const Process* process)
{
    auto tty = ProcessTerminal(process);
    return tty ? tty->owner : ConsoleTerminalOwner();
}

void ClearTerminalInput(Process* process)
{
    auto tty = ProcessTerminal(process);
    if (tty)
    {
        tty->head = tty->count = 0;
        tty->overflow = false;
    }
    else
    {
        ClearKeyboard();
    }
}

KeyboardRead ReadProcessTerminal(char& ch)
{
    auto tty = ProcessTerminal(GetRunningProcess());
    if (!tty)
    {
        return ReadKeyboard(ch);
    }
    if (tty->overflow)
    {
        tty->overflow = false;
        return KeyboardRead::Overflow;
    }
    if (!tty->count)
    {
        return KeyboardRead::Empty;
    }
    ch = tty->input[tty->head];
    tty->head = (tty->head + 1) % 256;
    --tty->count;
    return KeyboardRead::Character;
}

bool ProcessTerminalReady()
{
    auto tty = ProcessTerminal(GetRunningProcess());
    return tty ? tty->count || tty->overflow : KeyboardReady();
}

void TerminalClear()
{
    auto tty = ProcessTerminal(GetRunningProcess());
    if (!tty)
    {
        clrscr();
        return;
    }
    memset(tty->cells, ' ', sizeof(tty->cells));
    tty->x = tty->y = 0;
    ++tty->revision;
}

void TerminalPut(char ch)
{
    auto tty = ProcessTerminal(GetRunningProcess());
    if (!tty)
    {
        Display::SharedDisplay()->putc(ch);
        return;
    }
    SerialCharacter(ch);
    asm volatile("outb %0, $0xe9" ::"a"((uint8_t)ch));
    if (ch == '\n')
    {
        tty->x = 0;
        ++tty->y;
    }
    else if (ch == '\r')
    {
        tty->x = 0;
    }
    else if (ch == '\b')
    {
        if (tty->x)
        {
            --tty->x;
        }
        else if (tty->y)
        {
            --tty->y;
            tty->x = tty->columns - 1;
        }
        tty->cells[tty->y * tty->columns + tty->x] = ' ';
    }
    else if (ch == '\t')
    {
        do
        {
            TerminalPut(' ');
        } while (tty->x % 4);
        return;
    }
    else if ((uint8_t)ch >= 32)
    {
        tty->cells[tty->y * tty->columns + tty->x] = ch;
        if (++tty->x == tty->columns)
        {
            tty->x = 0;
            ++tty->y;
        }
    }
    if (tty->y >= tty->rows)
    {
        for (unsigned i = 0; i < (tty->rows - 1) * tty->columns; ++i)
        {
            tty->cells[i] = tty->cells[i + tty->columns];
        }
        memset(tty->cells + (tty->rows - 1) * tty->columns, ' ', tty->columns);
        tty->y = tty->rows - 1;
    }
    ++tty->revision;
}

void ReleaseTerminalController(Process* process)
{
    for (unsigned i = 0; i < HACKOS_TTY_LIMIT; ++i)
    {
        auto& tty = terminals[i];
        if (tty.controller != process->pid)
        {
            continue;
        }
        // Keep the endpoint alive while descendants return terminal ownership.
        for (auto p = ready_queue; p; p = p->next)
        {
            if (p->terminalId == i + 1 && p->state != PROCESS_STATE_ZOMBIE)
            {
                TerminateProcess(p->pid);
            }
        }
        memset(&tty, 0, sizeof(tty));
        tty.columns = 80;
        tty.rows = 25;
    }
}

uint64_t UserTerminalSession(uint64_t operation, uint64_t value, void* buffer)
{
    auto caller = GetRunningProcess();
    if (!caller->sessionId)
    {
        return HACKOS_DENIED;
    }
    TerminalSession* tty = nullptr;
    for (auto& item : terminals)
    {
        if (item.controller == caller->pid)
        {
            tty = &item;
        }
    }
    if (operation == HACKOS_TTY_CREATE)
    {
        if (tty)
        {
            return HACKOS_ERROR;
        }
        auto window = KernelWindowManager->GetWindow(value);
        if (!window || window->GetOwner() != caller->pid)
        {
            return HACKOS_DENIED;
        }
        for (unsigned i = 0; i < HACKOS_TTY_LIMIT; ++i)
        {
            if (terminals[i].controller)
            {
                continue;
            }
            auto& item = terminals[i];
            memset(&item, 0, sizeof(item));
            item.columns = 80;
            item.rows = 25;
            item.controller = caller->pid;
            item.columns = window->State().width / 8;
            item.rows = window->State().height / 16;
            if (item.columns > 160)
            {
                item.columns = 160;
            }
            if (item.columns < 1)
            {
                item.columns = 1;
            }
            if (item.rows > 60)
            {
                item.rows = 60;
            }
            if (item.rows < 1)
            {
                item.rows = 1;
            }
            char title[] = "Terminal 1";
            title[9] += i;
            window->SetTitle(title);
            auto& screen = Display::SharedDisplay()->framebuffer;
            uint32_t x = 40 + i * 28, y = 40 + i * 28;
            if (x + window->GetSizeX() + 1 < screen.width && y + window->GetSizeY() + 132 < screen.height)
            {
                window->MoveWindow(x, y);
            }
            memset(item.cells, ' ', sizeof(item.cells));
            auto pid = LoadUserProgramTask("~/USR/BIN/SHELL");
            if (!pid)
            {
                memset(&item, 0, sizeof(item));
                item.columns = 80;
                item.rows = 25;
                return HACKOS_ERROR;
            }
            for (auto p = ready_queue; p; p = p->next)
            {
                if (p->pid != pid)
                {
                    continue;
                }
                p->terminalId = i + 1;
                p->systemShell = true;
                p->jobOwnerPid = 0;
                p->processGroup = pid;
                p->terminalReturnPid = 0;
                item.shell = item.owner = pid;
            }
            return pid;
        }
        return HACKOS_ERROR;
    }
    if (!tty)
    {
        return HACKOS_DENIED;
    }
    if (operation == HACKOS_TTY_STATE)
    {
        HackosTerminalState state = {tty->revision, tty->shell, tty->columns,   tty->rows,
                                     tty->x,        tty->y,     tty->caretBack, tty->caret};
        return caller->userSpace->CopyToUser((uint64_t)buffer, &state, sizeof(state)) ? 0 : HACKOS_ERROR;
    }
    if (operation == HACKOS_TTY_CELLS_READ)
    {
        return caller->userSpace->CopyToUser((uint64_t)buffer, tty->cells, sizeof(tty->cells)) ? tty->revision
                                                                                               : HACKOS_ERROR;
    }
    if (operation == HACKOS_TTY_INPUT)
    {
        if (value > 255)
        {
            return HACKOS_ERROR;
        }
        if (value == HACKOS_KEY_INTERRUPT)
        {
            if (tty->owner)
            {
                RequestTerminalInterrupt(tty->owner);
            }
        }
        else if (value == HACKOS_KEY_STOP)
        {
            if (tty->owner)
            {
                RequestTerminalStop(tty->owner);
            }
        }
        else if (value)
        {
            if (tty->count == 256)
            {
                tty->head = tty->count = 0;
                tty->overflow = true;
            }
            else
            {
                tty->input[(tty->head + tty->count++) % 256] = value;
            }
        }
        WakeTerminalReaders();
        return 0;
    }
    if (operation == HACKOS_TTY_RESIZE)
    {
        unsigned columns = value & 0xffff, rows = value >> 16;
        if (value > 0xffffffff || columns < 1 || columns > 160 || rows < 1 || rows > 60)
        {
            return HACKOS_ERROR;
        }
        if (columns != tty->columns || rows != tty->rows)
        {
            // Keep rows and the cursor aligned when the grid changes width.
            // Static scratch is protected by the syscall preemption guard.
            static char resized[HACKOS_TTY_CELLS];
            memset(resized, ' ', sizeof(resized));
            unsigned copyRows = rows < tty->rows ? rows : tty->rows;
            unsigned copyColumns = columns < tty->columns ? columns : tty->columns;
            for (unsigned row = 0; row < copyRows; ++row)
            {
                memcpy(resized + row * columns, tty->cells + row * tty->columns, copyColumns);
            }
            memcpy(tty->cells, resized, sizeof(resized));
            tty->columns = columns;
            tty->rows = rows;
            if (tty->x >= columns)
            {
                tty->x = columns - 1;
            }
            if (tty->y >= rows)
            {
                tty->y = rows - 1;
            }
            ++tty->revision;
        }
        return 0;
    }
    return HACKOS_ERROR;
}

uint64_t UserDesktop(uint64_t operation, uint64_t value, HackosDesktopEntry* buffer)
{
    auto caller = GetRunningProcess();
    if (!caller->desktopController || !caller->sessionId)
    {
        return HACKOS_DENIED;
    }
    HackosDesktopEntry entry = {};
    if (operation == HACKOS_DESKTOP_SCREEN)
    {
        entry.width = Display::SharedDisplay()->framebuffer.width;
        entry.height = Display::SharedDisplay()->framebuffer.height;
    }
    else if (operation == HACKOS_DESKTOP_ENTRY)
    {
        unsigned index = 0;
        for (auto p = ready_queue; p; p = p->next)
        {
            if (p->sessionId != caller->sessionId || p == caller || p->state == PROCESS_STATE_ZOMBIE)
            {
                continue;
            }
            for (auto handle : p->windows)
            {
                auto window = KernelWindowManager->GetWindow(handle);
                if (!window || window->GetOwner() != p->pid)
                {
                    continue;
                }
                if (index++ != value)
                {
                    continue;
                }
                entry.handle = handle;
                entry.owner = p->pid;
                entry.mode = window->Mode();
                entry.focused = window->State().focused;
                const char* title = window->GetTitle();
                for (unsigned i = 0; i < 29 && title[i]; ++i)
                {
                    entry.title[i] = title[i];
                }
            }
        }
        if (!entry.handle)
        {
            return 0;
        }
    }
    else
    {
        auto window = KernelWindowManager->GetWindow(value);
        if (!window)
        {
            return HACKOS_ERROR;
        }
        bool allowed = false;
        for (auto p = ready_queue; p; p = p->next)
        {
            if (p->pid == window->GetOwner() && p->sessionId == caller->sessionId && p->state != PROCESS_STATE_ZOMBIE)
            {
                allowed = true;
            }
        }
        if (!allowed)
        {
            return HACKOS_DENIED;
        }
        if (operation == HACKOS_DESKTOP_PANEL && window->GetOwner() == caller->pid)
        {
            const auto& screen = Display::SharedDisplay()->framebuffer;
            auto state = window->State();
            if (KernelWindowManager->HasPanel() || state.x != 1 || state.y != screen.height - 132 ||
                state.width != screen.width - 2 || state.height != 100)
            {
                return HACKOS_ERROR;
            }
            KernelWindowManager->SetPanel(window);
            return 0;
        }
        if (operation == HACKOS_DESKTOP_BACKGROUND && window->GetOwner() == caller->pid)
        {
            const auto& screen = Display::SharedDisplay()->framebuffer;
            auto state = window->State();
            if (screen.height <= 164 || state.x != 1 || state.y != 1 || state.width != screen.width - 2 ||
                state.height != screen.height - 164)
            {
                return HACKOS_ERROR;
            }
            return KernelWindowManager->SetBackground(window) ? 0 : HACKOS_ERROR;
        }
        if (window->GetOwner() == caller->pid)
        {
            return HACKOS_DENIED;
        }
        if (operation == HACKOS_DESKTOP_FOCUS)
        {
            return KernelWindowManager->Activate(window);
        }
        if (operation == HACKOS_DESKTOP_MINIMIZE)
        {
            return KernelWindowManager->ChangeMode(window, HACKOS_WINDOW_MINIMIZE);
        }
        if (operation == HACKOS_DESKTOP_CLOSE)
        {
            KernelWindowManager->RequestClose(window);
            return 0;
        }
        return HACKOS_ERROR;
    }
    return caller->userSpace->CopyToUser((uint64_t)buffer, &entry, sizeof(entry)) ? 1 : HACKOS_ERROR;
}

uint64_t UserSessionLogout()
{
    auto caller = GetRunningProcess();
    if (!caller->sessionId || (!caller->systemShell && !caller->desktopController))
    {
        return HACKOS_DENIED;
    }
    // Exit the leader through the ordinary completion path, ending all descendants.
    TerminateProcess(caller->sessionId);
    ExitUserProcess(0);
}
