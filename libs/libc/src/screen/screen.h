#ifndef SCREEN_H
#define SCREEN_H

#include "stdint.h"
#include "font/font.h"

#include "data_structures/vector.h"

#include "screen/framebuffer.h"

#define COLOUR(r, g, b, a) ((a << 24) | (r << 16) | (g << 8) | b)

struct Console 
{
	uint32_t bgColour, fontColour;
	uint32_t current_x, current_y;
    uint32_t con_width, con_height;
    uint32_t char_width, char_height;
};

class Display 
{
public:
    int puts(const char* string);
    int putc(char ch);
    void put_backspace();
    void clear();

    static Display* SharedDisplay();

public:
    static Display* s_Display;
	Console console;
    
    Framebuffer* framebuffer;
    PSF1_FONT* font;
};

/* Public kernel API */
void clrscr();
void printf_backspace();

void InitialiseDisplay(Framebuffer* fb, PSF1_FONT* font);

#endif