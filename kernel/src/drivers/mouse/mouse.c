#include "mouse.h"

#include "arch/ports.h"
#include "arch/interrupts/isr.h"

#include "drivers/screen/screen.h"

#include "lib/string.h"
#include "lib/memory.h"
#include "lib/function.h"
#include "lib/stdio.h"

static uint8_t offset;
static uint8_t buffer[3];
static uint8_t buttons;

static double mouse_x;
static double mouse_y;

static int8_t button_state[3];
static double div;

static uint32_t _width, _height;
static uint32_t coordXOld, coordYOld;

static void mouse_callback(registers_t* regs)
{
    uint8_t status = __inb(0x64);
    if (!(status & 0x20))
        return;
    
    buffer[offset] = __inb(0x60);

    offset = (offset + 1) % 3;
    if(offset == 0)
    {
        if(buffer[1] != 0 || buffer[2] != 0)
        {
            double deltaX = buffer[1] / (double)_width;
            double deltaY = -buffer[2] / (double)_height;

            mouse_x += deltaX;
            mouse_y += deltaY;

            //The mouse coordinates are between -1 and 1, not screen coordinates
            if(mouse_x < -1) mouse_x = -1;
            if(mouse_x > 1) mouse_x = 1;
            if(mouse_y < -1) mouse_y = 1;
            if(mouse_y > 1) mouse_y = 1;
        }

        for(uint8_t i = 0; i < 3; i++)
        {
            if((buffer[0] & (0x1 << i)) != (buttons & (0x1 << i)))
            {
                button_state[i] = buttons & (0x1 << i);
            }
        }

        buttons = buffer[0];
    }
}

void InitialiseMouse(double mouseDivisor, uint32_t width, uint32_t height) 
{
    div = mouseDivisor;
    _width = width;
    _height = height;

    offset = 0;
    buttons = 0;

    mouse_y = 0;
    mouse_x = 0;
    memset(button_state, 0, 3 * sizeof(int8_t));
    
    __outb(0x64, 0xA8);
    __outb(0x64, 0x20); // command 0x60 = read controller command byte
    uint8_t status = __inb(0x60) | 2;
    __outb(0x64, 0x60); // command 0x60 = set controller command byte
    __outb(0x60, status);

    __outb(0x64, 0xD4);
    __outb(0x60, 0xF4);
    __inb(0x60);

    RegisterInterruptHandler(IRQ12, mouse_callback);
}

double GetMouseX()
{
    return mouse_x;
}

double GetMouseY()
{
    return mouse_y;
}

uint8_t get_mouse_button(uint8_t index)
{
    if(index >= 3)
    {
        return;
    }

    return button_state[index];
}