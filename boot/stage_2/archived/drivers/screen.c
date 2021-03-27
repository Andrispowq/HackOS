#include "screen.h"
#include "../cpu/ports.h"
#include "../libc/memory.h"
#include "../libc/string.h"

static DISPLAY* curr_display;

/* Declaration of private functions */
void clear_screen();
int put_char(char ch);
int put_string(const char* str);
void put_backspace();
void scroll_up();

int get_cursor_offset();
void set_cursor_offset(int offset);
int get_offset(int col, int row);
int get_offset_row(int offset);
int get_offset_col(int offset);

DISPLAY* get_current_display()
{
    return curr_display;
}

void init_display()
{
    curr_display = (DISPLAY*) kmalloc(sizeof(DISPLAY));

    curr_display->width = MAX_COLS;
    curr_display->height = MAX_ROWS;
    curr_display->console.fgColour = 0x0;
    curr_display->console.bgColour = 0xF;
    curr_display->console.current_x = 0;
    curr_display->console.current_y = 0;
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
    int screen_size = MAX_COLS * MAX_ROWS;
    int i;
    char *screen = (char*)VIDEO_ADDRESS;

    for (i = 0; i < screen_size; i++) 
    {
        screen[i * 2 + 0] = ' ';
        screen[i * 2 + 1] = COLOUR(curr_display->console.fgColour,
            curr_display->console.bgColour);
    }

    set_cursor_offset(get_offset(0, 0));
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

int put_char(char ch) 
{
    unsigned char *vidmem = (unsigned char*) VIDEO_ADDRESS;

    int cursor = get_cursor_offset();
    int row = get_offset_row(cursor);
    int col = get_offset_col(cursor);

    if(ch == '\n')
    {
        cursor = get_offset(0, row + 1);
    }
    else if(ch == '\r')
    {
        cursor = get_offset(0, row);
    }
    else if(ch == '\t')
    {
        cursor = get_offset(col + 4, row);
    }
    else if (ch == 0x08) /* Backspace */
    {
        cursor -= 2;
        vidmem[cursor] = ' ';
        vidmem[cursor + 1] = COLOUR(curr_display->console.fgColour, 
            curr_display->console.bgColour);
    }
    else
    {
        vidmem[cursor] = ch;
        vidmem[cursor + 1] = COLOUR(curr_display->console.fgColour, 
            curr_display->console.bgColour);
        cursor = cursor + 2;
    }

    /* Check if the offset is over screen size and scroll */
    if (cursor >= MAX_ROWS * MAX_COLS * 2) 
    {
        scroll_up();
        cursor -= 2 * MAX_COLS;
    }

    set_cursor_offset(cursor);
    return cursor;
}

void scroll_up()
{
    //Move everything up
    int i;
    for (i = 0; i < MAX_ROWS - 1; i++)
    {
        memcpy((void*)(VIDEO_ADDRESS + get_offset(0, i)),
            (const void*)(VIDEO_ADDRESS + get_offset(0, i + 1)), 
            MAX_COLS * 2);
    }

    //Clear the last row
    memset((void*)(VIDEO_ADDRESS + get_offset(0, MAX_ROWS - 1)),
        0, MAX_COLS * 2);
}

int get_cursor_offset() 
{
    outb(REG_SCREEN_CTRL, 14);
    int offset = inb(REG_SCREEN_DATA) << 8;

    outb(REG_SCREEN_CTRL, 15);
    offset += inb(REG_SCREEN_DATA);

    return offset * 2;
}

void set_cursor_offset(int offset) 
{
    offset /= 2;

    curr_display->console.current_x = get_offset_col(offset);
    curr_display->console.current_y = get_offset_row(offset);

    outb(REG_SCREEN_CTRL, 14);
    outb(REG_SCREEN_DATA, (unsigned char)(offset >> 8));
    outb(REG_SCREEN_CTRL, 15);
    outb(REG_SCREEN_DATA, (unsigned char)(offset & 0xff));
}

int get_offset(int col, int row) { return 2 * (row * MAX_COLS + col); }
int get_offset_row(int offset) { return offset / (2 * MAX_COLS); }
int get_offset_col(int offset) { return (offset - (get_offset_row(offset) * 2 * MAX_COLS)) / 2; }