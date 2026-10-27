#include "keyboard.h"
#include "input.h"

#include "drivers/mouse/mouse.h"
#include "hackos/runtime/event_queue.h"
#include "arch/x86_64/ports.h"
#include "arch/x86_64/interrupts/idt.h"
#include "arch/x86_64/interrupts/guard.h"

static KeyboardInput decoder;
static EventQueue<char, 255> terminalInput;

static void keyboard_callback(Registers*)
{
    decoder.PushScancode(__inb(0x60));
    decoder.TakeInterrupt();

    char key;
    while (decoder.Pop(key))
    {
        QueueKey((uint8_t)key);
    }
}

void ClearKeyboard(bool queued)
{
    InterruptGuard guard;
    decoder.Clear();
    terminalInput.Clear();

    if (queued) DiscardQueuedKeys();
}

void DeliverKeyboard(uint32_t key)
{
    InterruptGuard guard;
    if (key == 3) terminalInput.Clear();

    terminalInput.Push((char)key);
}

void KeyboardOverflow()
{
    InterruptGuard guard;
    terminalInput.Overflow();
}

void InitKeyboard()
{
    RegisterInterruptHandler(IRQ1, keyboard_callback);
}

KeyboardRead ReadKeyboard(char& ch)
{
    InterruptGuard guard;
    if (terminalInput.TakeOverflow()) return KeyboardRead::Overflow;

    return terminalInput.Pop(ch) ? KeyboardRead::Character : KeyboardRead::Empty;
}
