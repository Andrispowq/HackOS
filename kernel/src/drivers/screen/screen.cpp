#include "drivers/serial/serial.h"
#include "screen.h"
#include "window_manager.h"
#include "drivers/mouse/mouse.h"
#include "arch/x86_64/interrupts/guard.h"
#include "arch/x86_64/timer/pit.h"

#include "arch/x86_64/ports.h"
#include "arch/x86_64/paging/paging.h"

#include "lib/memory.h"
#include "lib/string.h"

Display* Display::s_Display = nullptr;

Display display;

Display* Display::SharedDisplay()
{
    if (s_Display == nullptr)
    {
        s_Display = &display;
    }

    return s_Display;
}

extern PageTableManager KernelDirectory;

void InitialiseDisplay(Framebuffer framebuffer, PSF1_FONT* font)
{
    uint64_t fb_addr = (uint64_t)framebuffer.address;
    uint64_t fb_size = (uint64_t)framebuffer.pitch * framebuffer.height * 4;
    for (uint64_t i = fb_addr; i < fb_addr + fb_size + 0x1000; i += 0x1000)
    {
        KernelDirectory.MapMemory(i, i,
                                  (uint64_t)PageTableFlags::Present | (uint64_t)PageTableFlags::Writable |
                                      (uint64_t)PageTableFlags::CacheDisabled | (uint64_t)PageTableFlags::WriteThrough);
    }

    uint64_t backbufferMemory = 0xD0000000;
    for (uint64_t i = backbufferMemory; i < backbufferMemory + fb_size + 0x1000; i += 0x1000)
    {
        KernelDirectory.MapMemory(i, (uint64_t)PageFrameAllocator::SharedAllocator()->RequestPage());
    }

    memset((void*)backbufferMemory, 0, fb_size);

    Framebuffer backbuffer;
    backbuffer.address = (uint32_t*)backbufferMemory;
    backbuffer.width = framebuffer.width;
    backbuffer.height = framebuffer.height;
    backbuffer.bpp = framebuffer.bpp;
    backbuffer.pitch = framebuffer.pitch;
    display.backbuffer = backbuffer;

    display.framebuffer = framebuffer;
    display.InitialiseComposition((backbufferMemory + fb_size + 8191) & ~4095ULL, fb_size);
    display.console.Configure(HACKOS_TERMINAL_RESET, 0, framebuffer.width, framebuffer.height);
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

void Display::Refresh()
{
    // Coalesce small user uploads and cursor events between frames. Keep dirty
    // set until the final update is displayed, even when input has stopped.
    if ((dirty || cursorDirty) && tick - lastFrameTick >= TimerTicksForMilliseconds(16))
    {
        DrawBackbuffer();
    }
}

void Display::InitialiseComposition(uint64_t memory, uint64_t bytes)
{
    uint64_t allocation = (bytes + 4095) & ~4095ULL;
    for (uint64_t offset = 0; offset < allocation * 2; offset += 4096)
    {
        KernelDirectory.MapMemory(memory + offset, (uint64_t)PageFrameAllocator::SharedAllocator()->RequestPage());
    }

    composition = framebuffer;
    composition.address = (uint32_t*)memory;
    previous = framebuffer;
    previous.address = (uint32_t*)(memory + allocation);
    // The first presentation is unconditional, so previous needs no clearing.
}

void Display::Invalidate()
{
    InvalidateRect(0, 0, framebuffer.width, framebuffer.height);
}

void Display::InvalidateRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    damage.Include(x, y, width, height, framebuffer.width, framebuffer.height);
    if (!damage.Empty())
    {
        dirty = true;
    }
}

void Display::DrawBackbuffer()
{
    PreemptionGuard guard;
    // Restore from the last composed scene, never read device memory. This also
    // removes the cursor before comparing/presenting a changed scene.
    if (presented && KernelWindowManager)
    {
        framebuffer.CopyRegion(composition, cursorX, cursorY, 16, 16);
    }

    if (dirty || !presented)
    {
        composition.CopyFrom(backbuffer, 0, 0);
        if (KernelWindowManager)
        {
            KernelWindowManager->Draw();
        }

        if (!presented || damage.Empty())
        {
            pixelsPresented += framebuffer.PresentChanged(composition, previous, !presented);
        }
        else
        {
            Framebuffer target = framebuffer, current = composition, saved = previous;
            Framebuffer* buffers[] = {&target, &current, &saved};
            for (auto buffer : buffers)
            {
                buffer->address += (uint64_t)damage.top * buffer->pitch + damage.left;
                buffer->width = damage.right - damage.left;
                buffer->height = damage.bottom - damage.top;
            }

            pixelsPresented += target.PresentChanged(current, saved, false);
        }
        ++sceneFrames;
    }

    if (KernelWindowManager)
    {
        GetMousePosition(cursorX, cursorY);
        DrawMouse(framebuffer, cursorX, cursorY);
    }

    dirty = false;
    damage = {};
    cursorDirty = false;
    presented = true;
    ++framesPresented;
    lastFrameTick = tick;
}

void Display::clear()
{
    PreemptionGuard guard;
    Invalidate();
    backbuffer.Clear(console.Background());
}

void Display::put_backspace()
{
    putc((char)0x08);
}

int Display::puts(const char* str)
{
    char c;
    int i = 0;
    while ((c = str[i++]) != 0)
    {
        putc(c);
    }

    return i;
}

bool Display::ConfigureTerminal(uint64_t key, uint64_t value)
{
    PreemptionGuard guard;
    if (!console.Configure(key, value, backbuffer.width, backbuffer.height))
    {
        return false;
    }
    clear();
    return true;
}

void putchar_at_xy(Framebuffer fb, char character, uint32_t x, uint32_t y)
{
    // Window chrome keeps its native font independently of terminal settings.
    DrawConsoleGlyph(fb, *display.font, (uint8_t)character, x, y, 8, display.font->psf1_Header->charsize, 0xffffffff);
}

static void empty_char_at(Framebuffer& fb, uint32_t x, uint32_t y)
{
    const Console& console = display.console;
    display.InvalidateRect(x * console.char_width, y * console.char_height, console.char_width, console.char_height);
    fb.DrawRect(x * console.char_width, y * console.char_height, console.char_width, console.char_height,
                console.Background());
}

int Display::putc(char ch)
{
    PreemptionGuard guard;
    SerialCharacter(ch);
    // QEMU's optional debug console mirrors text independently of the framebuffer.
    asm volatile("outb %0, $0xe9" ::"a"((uint8_t)ch));
    uint32_t& x = console.current_x;
    uint32_t& y = console.current_y;
    uint32_t width = console.con_width, height = console.con_height;
    if (!width || !height)
    {
        return 0;
    }

    if (ch == '\n')
    {
        x = 0;
        ++y;
    }
    else if (ch == '\r')
    {
        x = 0;
    }
    else if (ch == '\b')
    {
        if (x)
        {
            --x;
        }
        else if (y)
        {
            --y;
            x = width - 1;
        }
        else
        {
            return 0;
        }

        empty_char_at(backbuffer, x, y);
    }
    else if (ch == '\t')
    {
        do
        {
            putc(' ');
        } while (console.current_x % 4);
        return 0;
    }
    else
    {
        empty_char_at(backbuffer, x, y);
        DrawConsoleGlyph(backbuffer, *font, (uint8_t)ch, x * console.char_width, y * console.char_height,
                         console.char_width, console.char_height, console.Foreground());
        if (++x == width)
        {
            x = 0;
            ++y;
        }
    }

    if (y >= height)
    {
        console.Scroll(backbuffer);
        Invalidate();
        y = height - 1;
    }

    return 0;
}
