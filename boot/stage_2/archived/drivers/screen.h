#ifndef SCREEN_H
#define SCREEN_H

#include "../libc/stdint.h"

#define VIDEO_ADDRESS 0xB8000
#define MAX_ROWS 25
#define MAX_COLS 80
#define WHITE_ON_BLACK 0x0F
#define RED_ON_WHITE 0xF4

#define COLOUR(fg, bg) ((fg << 0x8) | bg)

/* Screen i/o ports */
#define REG_SCREEN_CTRL 0x3D4
#define REG_SCREEN_DATA 0x3D5

typedef struct _console 
{
	uint8_t bgColour, fgColour;
	uint16_t current_x, current_y;
} CONSOLE;

typedef struct 
{
	uint32_t width, height;
	CONSOLE console;

    int(*puts)(const char* string);
    int(*putc)(char character);
    void(*put_backspace)();
    void(*clear)();
} DISPLAY;

/* Public kernel API */
void clrscr();
void printf_backspace();

void init_display();
DISPLAY* get_current_display();

#endif