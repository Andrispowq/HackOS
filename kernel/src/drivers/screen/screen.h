#ifndef SCREEN_H
#define SCREEN_H

#include "lib/stdint.h"
#include "lib/font/font.h"

#include "lib/data_structures/vector.h"

#include "drivers/screen/framebuffer.h"
#include "drivers/screen/console.h"

#define COLOUR(r, g, b, a) ((a << 24) | (r << 16) | (g << 8) | b)

class Display
{
  public:
    int puts(const char* string);
    int putc(char ch);
    void put_backspace();
    void clear();
    bool ConfigureTerminal(uint64_t key, uint64_t value);

    void DrawBackbuffer();
    void Refresh();
    void Invalidate();
    void InvalidateRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    void InitialiseComposition(uint64_t memory, uint64_t bytes);

    static Display* SharedDisplay();

  public:
    static Display* s_Display;
    Console console;

    Framebuffer framebuffer;
    Framebuffer backbuffer;
    PSF1_FONT* font;
    bool dirty;
    bool cursorDirty = false;
    Framebuffer composition;

    // Debugger-visible counters; no serial output in the rendering hot path.
    uint64_t framesPresented = 0;
    uint64_t sceneFrames = 0;
    uint64_t pixelsPresented = 0;

  private:
    uint64_t lastFrameTick = 0;
    Framebuffer previous;
    FramebufferDamage damage;
    bool presented = false;
    uint32_t cursorX = 0, cursorY = 0;
};

/* Public kernel API */
void clrscr();
void printf_backspace();

void InitialiseDisplay(Framebuffer fb, PSF1_FONT* font);

#endif
