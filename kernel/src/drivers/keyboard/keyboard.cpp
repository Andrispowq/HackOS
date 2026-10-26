#include "keyboard.h"
#include "input.h"
#include "arch/x86_64/ports.h"
#include "arch/x86_64/interrupts/idt.h"
#include "arch/x86_64/interrupts/guard.h"

static KeyboardInput input;
static void keyboard_callback(Registers*) { input.PushScancode(__inb(0x60)); }
void InitKeyboard() { RegisterInterruptHandler(IRQ1, keyboard_callback); }
KeyboardRead ReadKeyboard(char& ch)
{
    InterruptGuard guard;
    if (input.DiscardOverflow()) return KeyboardRead::Overflow;
    return input.Pop(ch) ? KeyboardRead::Character : KeyboardRead::Empty;
}
