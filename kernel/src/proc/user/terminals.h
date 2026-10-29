#pragma once
#include "hackos/abi.h"
#include "drivers/keyboard/keyboard.h"
class Process;

struct TerminalSession
{
    uint64_t controller = 0, shell = 0, owner = 0, revision = 0;
    uint32_t columns = 80, rows = 25, x = 0, y = 0, caretBack = 0;
    bool caret = false, overflow = false;
    uint16_t head = 0, count = 0;
    char input[256] = {}, cells[HACKOS_TTY_CELLS] = {};
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
