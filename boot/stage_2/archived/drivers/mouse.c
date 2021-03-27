#include "mouse.h"
#include "../cpu/ports.h"
#include "../cpu/isr.h"
#include "../libc/string.h"
#include "../libc/memory.h"
#include "../libc/function.h"
#include "../libc/stdio.h"

static uint8_t offset;
static uint8_t buffer[3];
static uint8_t buttons;

static int8_t mouse_x;
static int8_t mouse_y;

static int8_t button_state[3];
static int div;

static void mouse_callback(registers_t* regs)
{ 
    uint8_t status = inb(0x64);
    if (!(status & 0x20))
        return;
    
    buffer[offset] = inb(0x60);

    offset = (offset + 1) % 3;
    if(offset == 0)
    {
        if(buffer[1] != 0 || buffer[2] != 0)
        {
            uint16_t* VideoMem = (uint16_t*)0xB8000;

            uint16_t orig = VideoMem[80 * mouse_y + mouse_x];
            VideoMem[80 * mouse_y + mouse_x] = ((orig & 0xF000) >> 0x4)
                                        | ((orig & 0x0F00) << 0x4)
                                        | ((orig & 0x00FF));

            mouse_x += buffer[1];
            mouse_y -= buffer[2];

            if(mouse_x < 0) mouse_x = 0;
            if(mouse_x > 79) mouse_x = 79;
            if(mouse_y < 0) mouse_y = 0;
            if(mouse_y > 24) mouse_y = 24;

            orig = VideoMem[80 * mouse_y + mouse_x];
            VideoMem[80 * mouse_y + mouse_x] = ((orig & 0xF000) >> 0x4)
                                        | ((orig & 0x0F00) << 0x4)
                                        | ((orig & 0x00FF));
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

void init_mouse(int movement_divider) 
{
    div = movement_divider;

    offset = 0;
    buttons = 0;

    mouse_y = 39;
    mouse_x = 12;
    memset(button_state, 0, 3 * sizeof(int8_t));
    
    outb(0x64, 0xA8);
    outb(0x64, 0x20); // command 0x60 = read controller command byte
    uint8_t status = inb(0x60) | 2;
    outb(0x64, 0x60); // command 0x60 = set controller command byte
    outb(0x60, status);

    outb(0x64, 0xD4);
    outb(0x60, 0xF4);
    inb(0x60);

    register_interrupt_handler(IRQ12, mouse_callback);
}

int8_t get_mouse_x()
{
    return mouse_x;
}

int8_t get_mouse_y()
{
    return mouse_y;
}

uint8_t get_mouse_button(uint8_t i)
{
    if(i >= 3) return;

    return button_state[i];
}