#ifndef SCREEN_H
#define SCREEN_H

#include "lib/stdint.h"
#include "lib/font/font.h"

#include "lib/data_structures/vector.h"

#include "drivers/screen/framebuffer.h"
#include "drivers/screen/console.h"

#define COLOUR(r, g, b, a) ((a << 24) | (r << 16) | (g << 8) | b)

struct PresentationLatency
{
    uint64_t samples = 0, totalTicks = 0, lastTicks = 0, maxTicks = 0;
    uint64_t started = 0, window = 0;
    bool pending = false, ready = false;
};

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
    void SetCaret(uint64_t owner, uint32_t back, bool enabled);
    void Invalidate();
    void InvalidateRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    void ObserveInteraction(unsigned kind, uint64_t window, bool ready);
    void WindowCommitted(uint64_t window);
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
    uint64_t pixelsComposed = 0, regionsComposed = 0;
    uint64_t renderTicks = 0, lastRenderTicks = 0, maxRenderTicks = 0;
    PresentationLatency latency[3] = {}; // text dispatch, drag geometry, resize geometry

  private:
    uint64_t lastFrameTick = 0;
    Framebuffer previous;
    FramebufferDamage damage;
    FramebufferDamageList damageRegions;
    void UpdateCaret();
    uint64_t caretOwner = 0, caretTick = 0;
    uint32_t caretBack = 0, caretX = 0, caretY = 0;
    bool caretEnabled = false, caretVisible = false;
    bool presented = false;
    uint32_t cursorX = 0, cursorY = 0;
};

/* Public kernel API */
void clrscr();
void printf_backspace();

void InitialiseDisplay(Framebuffer fb, PSF1_FONT* font);

#endif
