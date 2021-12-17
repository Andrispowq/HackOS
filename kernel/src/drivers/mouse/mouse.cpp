#include "mouse.h"

#include "arch/x86_64/ports.h"
#include "arch/x86_64/interrupts/idt.h"

#include "drivers/screen/screen.h"

#include "lib/string.h"
#include "lib/memory.h"
#include "lib/function.h"
#include "lib/stdio.h"

uint8_t MousePointer[] = 
{
    0b11111111, 0b11100000, 
    0b11111111, 0b10000000, 
    0b11111110, 0b00000000, 
    0b11111100, 0b00000000, 
    0b11111000, 0b00000000, 
    0b11110000, 0b00000000, 
    0b11100000, 0b00000000, 
    0b11000000, 0b00000000, 
    0b11000000, 0b00000000, 
    0b10000000, 0b00000000, 
    0b10000000, 0b00000000, 
    0b00000000, 0b00000000, 
    0b00000000, 0b00000000, 
    0b00000000, 0b00000000, 
    0b00000000, 0b00000000, 
    0b00000000, 0b00000000, 
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

static uint8_t MouseCycle = 0;
static uint8_t MousePacket[4];
static uint8_t MousePacketReady = 0;
static int MousePositionX, MousePositionY;
static int MousePositionOldX, MousePositionOldY;

static uint32_t Width, Height;
static uint8_t buttons;

static void ProcessMousePacket()
{
    if (!MousePacketReady)
    {
        return;
    }

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
    } else
    {
        MousePacket[2] = 256 - MousePacket[2];
        MousePositionY += MousePacket[2];

        if (yOF)
        {
            MousePositionY += 255;
        }
    }

    if (MousePositionX < 0) MousePositionX = 0;
    if (MousePositionX > Width - 1) MousePositionX = Width - 1;
    
    if (MousePositionY < 0) MousePositionY = 0;
    if (MousePositionY > Height - 1) MousePositionY = Height - 1;
    
    Display::SharedDisplay()->framebuffer.ClearMouseCursor(MousePointer, MousePositionOldX, MousePositionOldY);
    Display::SharedDisplay()->framebuffer.DrawMouseCursor(MousePointer, MousePositionX, MousePositionY, 0xffffffff);

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

    MousePacketReady = false;
    MousePositionOldX = MousePositionX;
    MousePositionOldY = MousePositionY;
}

static void mouse_callback(Registers* regs)
{
    uint8_t status = __inb(0x60);

    ProcessMousePacket();

    static uint8_t skip = 1;
    if (skip) 
    { 
        skip = 0; 
        return; 
    }

    switch(MouseCycle)
    {
    case 0:           
        if ((status & 0b00001000) == 0) 
            break;
        MousePacket[0] = status;
        MouseCycle++;
        break;
    case 1:           
        MousePacket[1] = status;
        MouseCycle++;
        break;
    case 2:            
        MousePacket[2] = status;
        MousePacketReady = 1;
        MouseCycle = 0;
        break;
    }
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

    MouseWrite(0xF6);
    MouseRead();

    MouseWrite(0xF4);
    MouseRead();

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
    if(index >= 3)
    {
        return 0;
    }

    return buttons & (0x1 << index);
}