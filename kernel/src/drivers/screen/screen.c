#include "screen.h"

#include "arch/ports.h"
#include "arch/paging/paging.h"

#include "lib/memory.h"
#include "lib/string.h"

static DISPLAY* curr_display;

/* Declaration of private functions */
void clear_screen();
int put_char(char ch);
int put_string(const char* str);
void put_backspace();
void scroll_up();

DISPLAY* GetCurrentDisplay()
{
    return curr_display;
}

void InitDisplay(FRAMEBUFFER framebuffer, PSF1_FONT* font)
{
    curr_display = (DISPLAY*) kmalloc(sizeof(DISPLAY));

    uint64_t fb_addr = framebuffer.address;
    uint64_t fb_size = framebuffer.width * framebuffer.height * 4;
    for(uint64_t i = fb_addr; i < fb_addr + fb_size + 0x1000; i += 0x1000)
    {
        MapMemory(i, i);
    }

    curr_display->framebuffer = framebuffer;
    curr_display->console.fontColour = 0xFF00FFFF;
    curr_display->console.bgColour = 0xFF505050;
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
    curr_display->console.current_x = 0;
    curr_display->console.current_y = 0;
}

void printf_backspace() 
{
    curr_display->put_backspace();
}

void PutPixel(uint32_t x, uint32_t y, uint32_t colour)
{
    curr_display->framebuffer.address[y * curr_display->framebuffer.width + x] = colour;
}

void DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t colour)
{
    uint64_t address = (uint64_t)curr_display->framebuffer.address;
    
    for(uint32_t _y = y; _y < (y + height); _y++)
    {
        uint32_t* _address = (uint32_t*)(address + _y * curr_display->framebuffer.pitch);
        for(uint32_t _x = x; _x < (x + width); _x++)
        {
            _address[_x] = colour;
        }
    }
}

/**********************************************************
 * Private kernel functions                               *
 **********************************************************/

void clear_screen()
{
    uint32_t* addr = (uint32_t*)(uint64_t)curr_display->framebuffer.address;
    uint32_t size = curr_display->framebuffer.width * 
        curr_display->framebuffer.height * 4;
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

    curr_display->console.current_x = x;
    curr_display->console.current_y = y;

    return 0;
}

void scroll_up()
{
    CONSOLE con = curr_display->console;
    FRAMEBUFFER fb = curr_display->framebuffer;

    uint32_t width = fb.width;
    uint32_t height = fb.height;
    uint32_t char_width = con.char_width;
    uint32_t char_height = con.char_height;

    uint32_t scroll_height = con.con_height - 1;
    uint64_t copy_size = char_height * width * 4;
    
    void* addr = fb.address;

    //Move everything up
    memcpy(addr, (void*)((uint64_t)addr + copy_size), copy_size * scroll_height);

    /*for(uint32_t i = 0; i < scroll_height; i++)
    {
        uint64_t offset = i * char_height * width * 4;
        void* to = (void*)((uint64_t)addr + offset);
        void* from = (void*)((uint64_t)to + copy_size);

        memcpy(to, from, copy_size);
    }*/

    //Clear the last row
    uint64_t offset = copy_size * scroll_height;
    void* last_row = (void*)((uint64_t)addr + offset);
    memset(last_row, con.bgColour, copy_size);
}