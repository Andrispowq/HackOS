#ifndef SCREEN_H
#define SCREEN_H

#include "libc/stdint.h"
#include "libc/font/font.h"
#include "drivers/framebuffer/framebuffer.h"

#define COLOUR(r, g, b, a) ((a << 24) | (r << 16) | (g << 8) | b)

typedef struct _console 
{
	uint32_t bgColour, fontColour;
	uint32_t current_x, current_y;
    uint32_t con_width, con_height;
    uint32_t char_width, char_height;
} CONSOLE;

typedef struct 
{
	CONSOLE console;
    FRAMEBUFFER framebuffer;
    PSF1_FONT* font;

    int(*puts)(const char* string);
    int(*putc)(char character);
    void(*put_backspace)();
    void(*clear)();
} DISPLAY;

/* Public kernel API */
void clrscr();
void printf_backspace();

void init_display(struct FramebufferInfo* fb, PSF1_FONT* font);
DISPLAY* get_current_display();

#endif