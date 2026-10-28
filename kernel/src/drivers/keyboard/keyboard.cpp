#include "keyboard.h"
#include "gui_input.h"
#include "proc/tasking/process.h"

#include "drivers/mouse/mouse.h"
#include "hackos/runtime/event_queue.h"
#include "arch/x86_64/ports.h"
#include "arch/x86_64/interrupts/idt.h"
#include "arch/x86_64/interrupts/guard.h"

static GuiKeyboardInput decoder;
static EventQueue<char, 255> terminalInput;

static void keyboard_callback(Registers*)
{
    GuiKey key;
    if (decoder.Push(__inb(0x60), key))
    {
        QueueGuiKey(key);
    }
}

void ClearKeyboard(bool queued)
{
    InterruptGuard guard;
    terminalInput.Clear();

    if (queued)
    {
        DiscardQueuedKeys();
    }
}

void DeliverKeyboard(uint32_t key)
{
    InterruptGuard guard;
    if (key == 3)
    {
        terminalInput.Clear();
    }

    terminalInput.Push((char)key);
    WakeTerminalReaders();
}

void KeyboardOverflow()
{
    InterruptGuard guard;
    terminalInput.Overflow();
    WakeTerminalReaders();
}

static bool keyboardRegistered;

bool KeyboardRegistered()
{
    return keyboardRegistered;
}

void InitKeyboard()
{
    RegisterInterruptHandler(IRQ1, keyboard_callback);
    keyboardRegistered = true;
}

KeyboardRead ReadKeyboard(char& ch)
{
    InterruptGuard guard;
    if (terminalInput.TakeOverflow())
    {
        return KeyboardRead::Overflow;
    }

    return terminalInput.Pop(ch) ? KeyboardRead::Character : KeyboardRead::Empty;
}

bool KeyboardReady()
{
    InterruptGuard guard;
    return terminalInput.Ready();
}

uint32_t KeyboardModifiers()
{
    InterruptGuard guard;
    return decoder.Modifiers();
}
