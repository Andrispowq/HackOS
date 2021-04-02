#ifndef SCREEN_H
#define SCREEN_H

#include "lib/stdint.h"
#include "lib/font/font.h"

#include "drivers/screen/framebuffer.h"

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

void InitDisplay(FRAMEBUFFER fb, PSF1_FONT* font);
DISPLAY* GetCurrentDisplay();
void SetCurrentDisplay(DISPLAY* display);

/* Special functions, might be put somewhere else in the future */
void PutPixel(uint32_t x, uint32_t y, uint32_t colour);
uint32_t GetPixel(uint32_t x, uint32_t y);
void DrawRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t colour);

void DrawMouseCursor(uint8_t* cursor, uint32_t x, uint32_t y, uint32_t colour);
void ClearMouseCursor(uint8_t* cursor, uint32_t x, uint32_t y);

#endif