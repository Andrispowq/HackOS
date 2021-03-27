#include "screen.h"
#include "cpu/ports.h"
#include "libc/memory.h"
#include "libc/string.h"

static DISPLAY* curr_display;

/* Declaration of private functions */
void clear_screen();
int put_char(char ch);
int put_string(const char* str);
void put_backspace();
void scroll_up();

DISPLAY* get_current_display()
{
    return curr_display;
}

void init_display(struct FramebufferInfo* fb, PSF1_FONT* font)
{
    curr_display = (DISPLAY*) kmalloc(sizeof(DISPLAY));

    curr_display->framebuffer = PrepareFramebuffer(fb);
    curr_display->console.fontColour = 0xFFFFFFFF;
    curr_display->console.bgColour = 0x0;
    curr_display->console.current_x = 0;
    curr_display->console.current_y = 0;
    curr_display->console.char_width = 8;
    curr_display->console.char_height = 16;
    curr_display->console.con_width = curr_display->framebuffer.width / 
            curr_display->console.char_width;
    curr_display->console.con_height = curr_display->framebuffer.height / 
            curr_display->console.char_height;
    curr_display->font = font;
    curr_display->puts = put_string;
    curr_display->putc = put_char;
    curr_display->put_backspace = put_backspace;
    curr_display->clear = clear_screen;
}

void clrscr()
{
    curr_display->clear();
}

void printf_backspace() 
{
    curr_display->put_backspace();
}

/**********************************************************
 * Private kernel functions                               *
 **********************************************************/

void clear_screen()
{
    uint32_t* addr = (uint32_t*)(uint64_t)curr_display->framebuffer.address;
    uint32_t size = curr_display->framebuffer.width * curr_display->framebuffer.height * 4;
    uint32_t colour = curr_display->console.bgColour;

    memset(addr, colour, size);
}

void put_backspace()
{
    put_char((char)0x08);
}

int put_string(const char* str)
{
    char c;
    int i = 0;
    while((c = str[i++]) != 0)
    {
        put_char(c);
    }

    return i;
}

void putchar_at(char character, uint32_t xOff, uint32_t yOff)
{
    uint32_t* addr = curr_display->framebuffer.address;
    uint32_t pitch = curr_display->framebuffer.width;
    
    uint32_t charWidth = curr_display->console.char_width;
    uint32_t charHeight = curr_display->console.char_height;

    PSF1_FONT* font = curr_display->font;

    char* fontPtr = font->glyphBuffer + (character * font->psf1_Header->charsize);
    for (uint32_t y = yOff * charHeight; y < (yOff * charHeight + charHeight); y++)
    {
        for (uint32_t x = xOff * charWidth; x < (xOff * charWidth + charWidth); x++)
        {
            if ((*fontPtr & (0b10000000 >> (x - (xOff * charWidth)))) > 0)
            {
                addr[y * pitch + x] = curr_display->console.fontColour;
            }

        }

        fontPtr++;
    }
}

void empty_char_at(uint32_t xOff, uint32_t yOff)
{
    uint32_t* addr = curr_display->framebuffer.address;
    uint32_t pitch = curr_display->framebuffer.width;
    
    uint32_t charWidth = curr_display->console.char_width;
    uint32_t charHeight = curr_display->console.char_height;

    PSF1_FONT* font = curr_display->font;

    for (uint32_t y = yOff * charHeight; y < (yOff * charHeight + charHeight); y++)
    {
        for (uint32_t x = xOff * charWidth; x < (xOff * charWidth + charWidth); x++)
        {
            addr[y * pitch + x] = curr_display->console.bgColour;

        }
    }
}

int put_char(char ch) 
{
    int xOff = 0;
    int yOff = 0;
    int new_x = -1;
    int new_y = -1;
    int to_put = 0; //0 -> no output, 1 -> put before offseting, 2 -> put after offseting

    uint32_t x = curr_display->console.current_x;
    uint32_t y = curr_display->console.current_y;
    uint32_t width = curr_display->console.con_width;
    uint32_t height = curr_display->console.con_height;

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
        }
    }
    if(yOff != 0)
    {
        y += yOff;
        if(y >= height)
        {
            y = (height - y);
        }
    }

    curr_display->console.current_x = x;
    curr_display->console.current_y = y;

    if(to_put == 2)
    {
        empty_char_at(x, y);
    }

    return 0;
}

void scroll_up()
{
    CONSOLE con = curr_display->console;
    FRAMEBUFFER fb = curr_display->framebuffer;

    uint32_t scroll_width = fb.width;
    uint32_t scroll_height = fb.height - con.con_height;
    
    void* addr = fb.address;

    //Move everything up
    memcpy((void*)((uint64_t)addr + scroll_width * 4), addr, scroll_width * scroll_height * 4);

    //Clear the last row
    memset((void*)((uint64_t)addr + scroll_width * scroll_height * 4),
        0, con.con_height * scroll_width * 4);
}