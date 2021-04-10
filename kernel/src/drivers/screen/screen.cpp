#include "screen.h"

#include "arch/x86_64/ports.h"
#include "arch/x86_64/paging/paging.h"

#include "lib/memory.h"
#include "lib/string.h"

Display* Display::s_Display = nullptr;

Display display;
Display* Display::SharedDisplay()
{
    if(s_Display == nullptr)
    {
        s_Display = &display;
    }

    return s_Display;
}

extern PageTableManager KernelDirectory;
void InitialiseDisplay(Framebuffer framebuffer, PSF1_FONT* font)
{
    uint64_t fb_addr = (uint64_t)framebuffer.address;
    uint64_t fb_size = framebuffer.width * framebuffer.height * 4;
    for(uint64_t i = fb_addr; i < fb_addr + fb_size + 0x1000; i += 0x1000)
    {
        KernelDirectory.MapMemory(i, i);
    }

    display.backbuffer.width = framebuffer.width;
    display.backbuffer.height = framebuffer.height;
    display.backbuffer.bpp = framebuffer.bpp;
    display.backbuffer.pitch = framebuffer.pitch;
    display.backbuffer.address = nullptr;

    display.framebuffer = framebuffer;
    display.console.fontColour = 0xFF00FFFF;
    display.console.bgColour = 0xFF505050;
    display.console.current_x = 0;
    display.console.current_y = 0;
    display.console.char_width = 8;
    display.console.char_height = 16;
    display.console.con_width = display.framebuffer.width / 
            display.console.char_width;
    display.console.con_height = display.framebuffer.height / 
            display.console.char_height;
    display.font = font;
}

void clrscr()
{
    display.clear();
    display.console.current_x = 0;
    display.console.current_y = 0;
}

void printf_backspace() 
{
    display.put_backspace();
}

void Display::clear()
{
    uint32_t* addr = framebuffer.address;
    uint32_t size = framebuffer.width * framebuffer.height * 4;
    uint32_t colour = console.bgColour;

    memset(addr, colour, size);
}

void Display::put_backspace()
{
    putc((char)0x08);
}

int Display::puts(const char* str)
{
    char c;
    int i = 0;
    while((c = str[i++]) != 0)
    {
        putc(c);
    }

    return i;
}

void putchar_at(char character, uint32_t xOff, uint32_t yOff)
{
    uint32_t* addr = display.framebuffer.address;
    uint32_t pitch = display.framebuffer.width;
    
    uint32_t charWidth = display.console.char_width;
    uint32_t charHeight = display.console.char_height;

    PSF1_FONT* font = display.font;

    char* fontPtr = (char*)font->glyphBuffer + (character * font->psf1_Header->charsize);
    for (uint32_t y = yOff * charHeight; y < (yOff * charHeight + charHeight); y++)
    {
        for (uint32_t x = xOff * charWidth; x < (xOff * charWidth + charWidth); x++)
        {
            if ((*fontPtr & (0b10000000 >> (x - (xOff * charWidth)))) > 0)
            {
                addr[y * pitch + x] = display.console.fontColour;
            }

        }

        fontPtr++;
    }
}

void empty_char_at(uint32_t xOff, uint32_t yOff)
{
    uint32_t* addr = display.framebuffer.address;
    uint32_t pitch = display.framebuffer.width;
    
    uint32_t charWidth = display.console.char_width;
    uint32_t charHeight = display.console.char_height;

    PSF1_FONT* font = display.font;

    for (uint32_t y = yOff * charHeight; y < (yOff * charHeight + charHeight); y++)
    {
        for (uint32_t x = xOff * charWidth; x < (xOff * charWidth + charWidth); x++)
        {
            addr[y * pitch + x] = display.console.bgColour;

        }
    }
}

void scroll_up()
{
    Console con = display.console;
    Framebuffer fb = display.framebuffer;

    uint32_t width = fb.width;
    uint32_t height = fb.height;
    uint32_t char_width = con.char_width;
    uint32_t char_height = con.char_height;

    uint32_t scroll_height = con.con_height - 1;
    uint64_t copy_size = char_height * width * 4;
    
    void* addr = fb.address;

    //Move everything up
    memcpy(addr, (void*)((uint64_t)addr + copy_size), copy_size * scroll_height);

    //Clear the last row
    uint64_t offset = copy_size * scroll_height;
    void* last_row = (void*)((uint64_t)addr + offset);
    memset(last_row, con.bgColour, copy_size);
}

int Display::putc(char ch) 
{
    int xOff = 0;
    int yOff = 0;
    int new_x = -1;
    int new_y = -1;
    int to_put = 0; //0 -> no output, 1 -> put before offseting, 2 -> put after offseting

    uint32_t x = display.console.current_x;
    uint32_t y = display.console.current_y;
    uint32_t width = display.console.con_width;
    uint32_t height = display.console.con_height;

    if(ch == '\n')
    {
        new_x = 0;
        yOff = 1;
    }
    else if(ch == '\r')
    {
        new_x = 0;
    }
    else if(ch == '\t')
    {
        xOff = 4;
    }
    else if (ch == 0x08)
    {
        xOff = -1;
        to_put = 2;
    }
    else
    {
        xOff = 1;
        to_put = 1;
    }

    if(to_put == 1)
    {
        putchar_at(ch, x, y);
    }

    if(to_put == 2)
    {
        empty_char_at(x - 1, y);
    }

    if(new_x != -1)
    {
        x = new_x;
    }
    if(new_y != -1)
    {
        y = new_y;
    }
    
    if(xOff != 0)
    {
        x += xOff;
        if(x >= width)
        {
            x = (width - x);
            yOff++;
        }
    }
    if(yOff != 0)
    {
        y += yOff;
        if(y >= height)
        {
            scroll_up();
            y = height - 1;
        }
    }

    display.console.current_x = x;
    display.console.current_y = y;

    return 0;
}

static uint32_t* middleBuffer = nullptr;
void Display::PresentBackbuffer()
{
    uint64_t size = framebuffer.width * framebuffer.height * 4;
    if(!middleBuffer)
    {
        middleBuffer = (uint32_t*)kmalloc(size);
    }

    asm("cli");

    memcpy(middleBuffer, framebuffer.address, size);
    memcpy(framebuffer.address, backbuffer.address, size);
    memcpy(middleBuffer, backbuffer.address, size);

    asm("sti");
}