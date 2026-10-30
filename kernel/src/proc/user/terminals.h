#pragma once
#include "hackos/abi.h"
#include "hackos/runtime/terminal_screen.h"
#include "hackos/runtime/terminal_appearance.h"
#include "hackos/runtime/terminal_paste.h"
#include "drivers/keyboard/keyboard.h"
class Process;

struct TerminalSession : TerminalScreen
{
    uint64_t controller = 0, shell = 0, owner = 0, revision = 0;
    uint32_t caretBack = 0;
    bool caret = false, overflow = false;
    uint16_t head = 0, count = 0;
    char input[256] = {};
    TerminalAppearance appearance = {};
    TerminalPaste paste = {};
    uint64_t window = 0;
};

extern TerminalSession terminals[HACKOS_TTY_LIMIT];
TerminalSession* ProcessTerminal(const Process* process);
uint64_t TerminalOwner(const Process* process);
void ClearTerminalInput(Process* process);
KeyboardRead ReadProcessTerminal(char& ch);
bool ProcessTerminalReady();
void TerminalPut(char ch);
void TerminalClear();
uint64_t UserTerminalSession(uint64_t operation, uint64_t value, void* buffer);
void ReleaseTerminalController(Process* process);
uint64_t UserDesktop(uint64_t operation, uint64_t value, HackosDesktopEntry* buffer);
uint64_t UserSessionLogout();

bool ConfigureWindowTerminal(TerminalSession& tty, uint64_t key, uint64_t value);
