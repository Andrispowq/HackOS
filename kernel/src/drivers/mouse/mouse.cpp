#include "mouse.h"
#include "packet.h"
#include "arch/x86_64/interrupts/guard.h"
#include "hackos/runtime/event_queue.h"
#include "hackos/abi.h"

#include "arch/x86_64/ports.h"
#include "arch/x86_64/interrupts/idt.h"

#include "drivers/screen/screen.h"

#include "lib/string.h"
#include "lib/memory.h"
#include "lib/function.h"
#include "lib/stdio.h"

uint8_t MousePointer[] = {
    0b11111111, 0b11100000, 0b11111111, 0b10000000, 0b11111110, 0b00000000, 0b11111100, 0b00000000,
    0b11111000, 0b00000000, 0b11110000, 0b00000000, 0b11100000, 0b00000000, 0b11000000, 0b00000000,
    0b11000000, 0b00000000, 0b10000000, 0b00000000, 0b10000000, 0b00000000, 0b00000000, 0b00000000,
    0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000, 0b00000000,
};

void MouseWait()
{
    uint64_t timeout = 100000;
    while (timeout--)
    {
        if ((__inb(0x64) & 0b10) == 0)
        {
            return;
        }
    }
}

void MouseWaitInput()
{
    uint64_t timeout = 100000;
    while (timeout--)
    {
        if (__inb(0x64) & 0b1)
        {
            return;
        }
    }
}

void MouseWrite(uint8_t value)
{
    MouseWait();
    __outb(0x64, 0xD4);
    MouseWait();
    __outb(0x60, value);
}

uint8_t MouseRead()
{
    MouseWaitInput();
    return __inb(0x60);
}

static MousePacketDecoder decoder;
static uint8_t mouseDevice;
static bool mouseEnabled;

bool MouseEnabled()
{
    return mouseEnabled;
}

uint8_t MouseDeviceId()
{
    return mouseDevice;
}

static int MousePositionX, MousePositionY;

static uint32_t Width, Height;
static uint8_t buttons;
static EventQueue<MouseState, 128> mouseEvents;
static bool interruptPending;

static void ProcessMousePacket()
{
    auto& MousePacket = decoder.bytes;

    uint8_t xNeg = MousePacket[0] & PS2XSign;
    uint8_t yNeg = MousePacket[0] & PS2YSign;
    uint8_t xOF = MousePacket[0] & PS2XOverflow;
    uint8_t yOF = MousePacket[0] & PS2YOverflow;

    if (!xNeg)
    {
        MousePositionX += MousePacket[1];

        if (xOF)
        {
            MousePositionX += 255;
        }
    }
    else
    {
        MousePacket[1] = 256 - MousePacket[1];
        MousePositionX -= MousePacket[1];

        if (xOF)
        {
            MousePositionX -= 255;
        }
    }

    if (!yNeg)
    {
        MousePositionY -= MousePacket[2];

        if (yOF)
        {
            MousePositionY -= 255;
        }
    }
    else
    {
        MousePacket[2] = 256 - MousePacket[2];
        MousePositionY += MousePacket[2];

        if (yOF)
        {
            MousePositionY += 255;
        }
    }

    if (MousePositionX < 0)
    {
        MousePositionX = 0;
    }

    if (MousePositionX > Width - 1)
    {
        MousePositionX = Width - 1;
    }

    if (MousePositionY < 0)
    {
        MousePositionY = 0;
    }

    if (MousePositionY > Height - 1)
    {
        MousePositionY = Height - 1;
    }

    if (MousePacket[0] & PS2LeftButton)
    {
        buttons |= 0x1 << 0;
    }
    else
    {
        buttons &= ~(0x1 << 0);
    }

    if (MousePacket[0] & PS2MiddleButton)
    {
        buttons |= 0x1 << 1;
    }
    else
    {
        buttons &= ~(0x1 << 1);
    }

    if (MousePacket[0] & PS2RightButton)
    {
        buttons |= 0x1 << 2;
    }
    else
    {
        buttons &= ~(0x1 << 2);
    }

    mouseEvents.Push({MousePositionX, MousePositionY, buttons, 0, decoder.Wheel()});
}

static void mouse_callback(Registers*)
{
    if (decoder.Push(__inb(0x60)))
    {
        ProcessMousePacket();
    }
}

static bool MouseReply(uint8_t& value)
{
    for (unsigned i = 0; i < 100000; i++)
    {
        uint8_t status = __inb(0x64);
        if ((status & 0x21) == 0x21)
        {
            value = __inb(0x60);
            return true;
        }
    }

    return false;
}

static bool MouseCommand(uint8_t value)
{
    for (unsigned attempt = 0; attempt < 3; attempt++)
    {
        MouseWrite(value);
        uint8_t reply;
        if (!MouseReply(reply))
        {
            return false;
        }

        if (reply == 0xfa)
        {
            return true;
        }

        if (reply != 0xfe)
        {
            return false;
        }
    }

    return false;
}

void InitialiseMouse(uint32_t width, uint32_t height)
{
    Width = width;
    Height = height;

    buttons = 0;

    __outb(0x64, 0xA8); //enabling the auxiliary device - mouse

    MouseWait();
    __outb(0x64, 0x20); //tells the keyboard controller that we want to send a command to the mouse
    MouseWaitInput();
    uint8_t status = __inb(0x60);
    status |= 0b10;
    MouseWait();
    __outb(0x64, 0x60);
    MouseWait();
    __outb(0x60, status); // setting the correct bit is the "compaq" status byte

    MouseCommand(0xf6);
    bool negotiated = true;
    const uint8_t rates[] = {200, 100, 80};
    for (auto rate : rates)
    {
        if (!MouseCommand(0xf3) || !MouseCommand(rate))
        {
            negotiated = false;
            break;
        }
    }

    uint8_t device = 0;
    if (negotiated && MouseCommand(0xf2) && MouseReply(device) && device == 3)
    {
        mouseDevice = 3;
    }
    else
    {
        // Reset restores the baseline three-byte protocol after a failed probe.
        if (MouseCommand(0xff))
        {
            uint8_t reply;
            MouseReply(reply);
            MouseReply(reply);
        }

        mouseDevice = 0;
    }

    decoder.Configure(mouseDevice);
    mouseEnabled = MouseCommand(0xf4);

    RegisterInterruptHandler(IRQ12, mouse_callback);
}

double GetMouseX()
{
    return (MousePositionX / (double)Width) * 2.0 - 1.0;
}

double GetMouseY()
{
    return (MousePositionY / (double)Height) * 2.0 - 1.0;
}

uint8_t GetMouseButton(uint8_t index)
{
    if (index >= 3)
    {
        return 0;
    }

    return buttons & (0x1 << index);
}

unsigned ReadMouse(MouseState& state)
{
    InterruptGuard guard;
    if (mouseEvents.TakeOverflow())
    {
        state = {MousePositionX, MousePositionY, buttons, 0, 0};
        return 2;
    }

    if (mouseEvents.Pop(state))
    {
        if (state.key == HACKOS_KEY_INTERRUPT)
        {
            interruptPending = false;
        }

        return 1;
    }

    // A full ingress queue must not swallow Ctrl-C. Lost input is reported
    // first, then the latched interrupt is delivered at a safe idle pass.
    if (interruptPending)
    {
        interruptPending = false;
        state = {0, 0, 0, HACKOS_KEY_INTERRUPT, 0};
        return 1;
    }

    return 0;
}

void GetMousePosition(uint32_t& x, uint32_t& y)
{
    InterruptGuard guard;
    x = MousePositionX;
    y = MousePositionY;
}

void DrawMouse(Framebuffer& framebuffer, uint32_t x, uint32_t y)
{
    // The scene is retained in RAM; no device reads or cursor-save buffer needed.
    for (uint32_t row = 0; row < 16; ++row)
    {
        for (uint32_t column = 0; column < 16; ++column)
        {
            if (MousePointer[row * 2 + column / 8] & (0x80 >> (column % 8)))
            {
                framebuffer.PutPixel(x + column, y + row, 0xffffffff);
            }
        }
    }
}

void QueueKey(uint32_t key)
{
    InterruptGuard guard;
    if (key == HACKOS_KEY_INTERRUPT)
    {
        interruptPending = true;
    }

    mouseEvents.Push({0, 0, 0, key, 0});
}

void DiscardQueuedKeys()
{
    InterruptGuard guard;
    unsigned count = mouseEvents.Count();
    MouseState state;
    for (unsigned i = 0; i < count; i++)
    {
        mouseEvents.Pop(state);
        if (!state.key)
        {
            mouseEvents.Push(state);
        }
    }

    interruptPending = false;
}
